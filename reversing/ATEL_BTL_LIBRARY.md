# The ATEL Btl syscall library, all 296 rows

2026-10-26. PROGRESS 1.8.9. Library 7 of `g_ffxAtelSysFuncLibs 0x1328558`, the table
`g_ffxAtelSysFuncLib07_Btl 0xC42628`, 296 rows of {start, poll, float getter, int getter}.
`FFX_Atel_Init 0x86D6D0` registers it at 0x86D874 (`push offset g_ffxAtelSysFuncLib07_Btl`, `push 7`).
The ABI is `ATEL_CONTEXT.md` 11.7 and 13. This note names every row that a shipped script calls, marks
the rest dead, and lists what each row means for lockstep. `../tools/apply_btl_names.py` puts the
names in the IDB. All addresses are IDA VAs at base 0x400000.

A script operand picks the library in bits 12..15 and the function in bits 0..11, so a Btl call is
operand 0x7NNN. Battle field scripts run in ATEL context 2 and monster AI in context 3
(`FFX_BtlEv_BuildScriptContext 0x798390`). [V]

Read only throughout. The handlers and their callees were decompiled through idalib on a scratch copy
of the IDB, with the prototypes of `FFX_Battle_GetActor`, `FFX_Btl_GetEnemyUnit` and
`FFX_Atel_GetActorBattleUnitByte` set in memory so the BTLUNIT fields print, nothing renamed and nothing
saved. The census walked the install with the same readers as the Core and Ch census, and no script
bytes were copied anywhere.

Marks: [V] read in the decompile or the disassembly, [I] inferred (a role read from the shape of a
wrapper, an engine field with no traced reader), [D] counted in shipped data. The table carries V or I
per row.

Two words used throughout. **self** is the calling actor's battle unit, the byte at atelActor+0xC0
(`FFX_Atel_GetActorBattleUnitByte`), 255 when the actor has none. A **sel** argument is a unit
selector, resolved to a mask by `FFX_BtlAi_ResolveTargetSelector 0x794330` or to one unit (the lowest
set bit) by `sub_7947F0`. MONSTER_AI.md has the selector values.

## 1. Short version

- **249 of the 296 indexes are called by shipped scripts, 47 are not.** 127,268 call sites over four
  sources. Every used row has a role, and the dead rows got one too, with the name left positional.
  No null row, no shared slot, no call past the end. Row 296 (0xC438A8) is the first entry of the aeon
  transition table, which `FFX_Btl_GetAeonTransitionEffect 0x7AF060` indexes at 0x7AF078. [D] [V]
- **Names follow the slot suffix form, `FFX_AtelSys_Btl_NNN_<Role>_<slot>`**, as in Core and Ch. Ten
  rows already had descriptive names (the nine AI and property rows of MONSTER_AI.md and
  `FFX_BtlAtelOp_SetUnitMoveSpeed`), all of them right. The table comment and a data label inside the
  table are wrong. Section 3.
- **Battle replicates actions, and four row groups read something an action relay does not carry.**
  This is the main finding. Section 5.
  - **Input.** The message waits btl:67 and btl:169 resume on the step the LOCAL confirm button is
    pressed (`sub_8B8530`, `g_ffxMenuPadHeldTrig`). 1,131 calls. [V]
  - **Sound pacing.** The voiced line pattern (btl:100, 101, 103, 104, 109, 124 and 169, about 850
    calls each) waits for, or returns into the script, the state of the local audio device. [V]
  - **The booster.** btl:222 returns 0 while the local battle fast forward is on and the save option
    bit 9 otherwise. 830 calls, one per voiced line. [V]
  - **The language.** Every kill a script causes (btl:14, 24, 158, 171) pays its spoils through
    `FFX_Btl_PayKillRewards`, which swaps item 0x2065 for another item and count on language 11. The
    message rows lay out text by language, and btl:220 copies language text into BTLUNIT. [V]
- **Eight rows draw from a stream directly**, all of them seeded battle streams: 4 (btl:16, 37, 231,
  249, 251), 5 (231), 7 (105), 17 (235), 18 (243, 251). Four more reach stream 9 and the kill reward
  streams through the hit and kill code (14, 24, 158, 171). The other 21 RNG hits of the call graph are
  false paths, proved at the call site. Section 5.1. [V]
- **Two one slot globals outlive a battle.** `word_1136410` (btl:250, the forced roll id that turns
  btl:243 and 251 from a draw into a compare) and `dword_11363BC` (btl:100, the unit remap of 272 and
  273) are written only by those rows and reset by nothing, not even battle begin. A peer that joined
  later reads a different value. [V]

## 2. The census [D]

Sources, all read from `FFX_Data.vbf`:

| source | files | files with a Btl call | Btl calls | rows used |
|---|---|---|---|---|
| event packages, `jppc/event/obj/*.ebp` | 397 | 124 | 907 | 11 |
| monster AI, `battle/mon/_mNNN/mNNN.bin` section 0 | 361 | 331 | 28,681 | 131 |
| battle field files, `battle/btl/*/*.bin` section at header +4 | 816 | 814 | 92,921 | 101 |
| `battle/btl/system_01/system_01.bin` | 1 | 1 | 4,759 | 148 |

The four sources overlap. By the set of sources that call a row: system_01 only 57, monster AI and
system_01 44, monster AI only 39, battle fields only 34, monster AI, fields and system_01 28, monster AI
and fields 19, fields and system_01 17, event packages only 8, and three rows shared between events and
battle files. system_01 is the common battle script (aeons, Yojimbo, Magus, the shared reactions), so it
calls more rows than any other single file.

Event packages call eleven rows: 2 RequestScriptedBattle (493), 21 DebugPrintInt, 53 GetForcedResult,
125 and 126 the debug switch (testbattle only), 138, 139 and 140 the encounter zone rows (camtest0 and
four field packages), 162 and 164 the system motion group, and 234 SetYojimboWord. That matches
`tools/ebp.py` `LIB_INFO` 7 = (0xC42628, 296, 234), whose last field is the highest index the event
packages call. Over all four sources the highest used index is 295.

The arity column comes from the handler's pops, in push order. The census also solves one from the
bytecode the same way as the Core census (stack depth at each `0x58`, one unknown for nested `0x35`
calls), seeded with the Core and Ch arities. Six rows pop inside a helper rather than in the handler
(7, 12, 83, 111, 133, 163), and the table gives the helper's pops.

The heaviest rows by calls: btl:24 SetProperty 32,859 in 952 files, 21 DebugPrintInt 20,814, 15
GetProperty 11,468, 153 SetBtlByteC9CD 5,976, 5 GetFieldAirborne 5,378, 171 SetSelfProperty 4,536, 63
FireEventAndWait 4,184, 178 SetSelfFloat 2,840, 30 CountUnitsInBoth 2,726, 7 PlayMotion 2,188, 11
AiAddCommand 1,622. The widest are 24 (952 files), 30 (818), 1 and 15 (807), 63 (786) and 33 (769).

Thirty used rows have a single call site: 14, 58, 97, 99, 121, 124, 129, 138, 139, 173, 180, 182, 187,
197, 208, 217, 218, 230, 232, 239, 259, 266, 267, 268, 274, 278, 280, 282, 284, 295.

Dead rows, 47: 4, 17, 19, 32, 77, 78, 79, 94, 96, 106, 108, 137, 144, 154, 155, 167, 185, 186, 191,
193, 194, 196, 198, 199, 200, 202, 203, 210, 212, 214, 219, 221, 225, 228, 229, 236, 240, 244, 245,
249, 270, 271, 283, 286, 287, 289, 290.

Row shapes, matching `ATEL_CONTEXT.md` 13.2: 252 rows have only the int getter, 17 only the float
getter, 14 have start and poll, 10 only a poll, 2 have start, poll and int getter (100, 109), and 1 has
poll and int getter (101). [V]

Many rows are copies or near copies. 7, 111 and 163 are one motion helper with a flag, 12, 83 and 133
one move helper, 23 and 211, 98 and 212, 81 and 188, 99 and 189 one effect body on self or on another
unit, 3, 6, 87, 264 and 274 one turn helper with different constants, 272 and 290 the same body less a
hack, 115, 291 and 293 one attach helper. Each copy is its own function, so no slot is shared. [V]

## 3. Names that were already there

Kept as they are, and checked against the bodies [V]:

- btl:11 `FFX_AtelSys_Btl_011_AiAddCommand_resi`, 90 `..._AiAddCommandNoReorder_resi`, 279
  `..._AiAddCommandPrio64_resi`, 16 `..._AiPickLiveTarget_resi`, 37 `..._AiPickTarget_resi`, 24
  `..._SetProperty_resi`, 171 `..._SetSelfProperty_resi`, 15 `..._GetProperty_resi`, 170
  `..._GetSelfProperty_resi` (MONSTER_AI.md). The bodies match.
- btl:19 `FFX_BtlAtelOp_SetUnitMoveSpeed`, which is `FFX_Ch_SetMoveSpeed` on the self CHR. Right, and
  dead in shipped data. `tools/ebp.py` calls it `BtlSetUnitMoveSpeed`, also right.

Wrong or misleading. The apply script changes none of these, except that it replaces a stale
`_wcstoul` name on 111's getter:

- **The table comment** on `g_ffxAtelSysFuncLib07_Btl` says 235 entries and an `FFX_BtlAtelOp_*`
  prefix. It is 296 entries and the prefix is `FFX_AtelSys_Btl_`. COOP_DESIGN.md already has 296.
- **`g_ffxAeonTransitionEffects` at 0xC43828** sits on row 288's start slot. It is the zero base of the
  aeon table that `FFX_Btl_GetAeonTransitionEffect` indexes by unit id (`lea esi, [ebx-8]`, `shl esi,
  4`, `add esi, offset unk_C438A8`), so the first real entry is unit 8 at 0xC438A8 and the label is a
  virtual base 128 bytes into the Btl table. Nothing references 0xC43828. Moving the label to 0xC438A8
  and saying "unit 8" in its comment would stop it reading as part of the table.
- **btl:111 resi 0x7A36C0** is positional now but still carries the `wcstoul` prototype (`const wchar_t
  *String, wchar_t **EndPtr, int Radix`). It is `int (int atelActor, int work, int *stack)` like every
  other getter. The apply script also takes over the name `_wcstoul` on this slot if the live IDB still
  has it (`ATEL_CONTEXT.md` 13.2).
- **`__RTC_NumErrors_806` 0x7A6DCA and `__RTC_NumErrors_807` 0x7A751F** are the `mov eax, 5` / `retn`
  tails of the btl:109 poll (0x7A6DB0) and the btl:162 poll (0x7A7500), split off as their own functions
  and named by a signature pass. They are not table slots. They belong back in the polls as chunks, or
  under names like `FFX_AtelSys_Btl_109_poll_Done`.
- **`FFX_Mot_IsFinished` 0x8392C0** returns the signed byte CHR +0x736, which is
  `m_motionRequestActive` in ffx_types.h and the byte ch:3 WaitMotionDone waits on. It is nonzero
  while a motion request runs, so the name has the sense backwards. btl:8 and btl:43 read it, and the
  table names them from the field, not from the function name. `FFX_Mot_IsRequestActive` would fit.
- **`FFX_Btl_Script_Btl222_GetValue` 0x785210** is right about where it is called from but says
  nothing about what it returns. `FFX_Btl_GetConfigBit9UnlessFfwd` would. Its comment already flags the
  booster read.

## 4. The families

Counts are rows, used plus dead.

### 4.1 AI command, targeting and hits (37 rows, 58,191 calls)

The monster AI staging rows of MONSTER_AI.md (11, 90, 279), the property get and set (15, 24, 170, 171,
168, 178) and the target picks (16, 37, 105). Around them: AiGetCurrentCommand (25), the issuer command
override (27), the per unit AI queue and list (55, 56, 241, 242), target mask edits (204, 205, 206, 255),
ability record queries (26, 120), the hit log (122, 228), and the command cost and preview rows (134,
214, 244, 245). btl:14 ApplyPendingHits is the one row that lands damage directly, called once from
system_01. btl:59 seals an ability for a mask. The counter tests 224 and 225 read the issuer's m_bDF1.

### 4.2 Chance, Yojimbo and Magus (18 rows, 223 calls)

All in system_01. The draws are 231 (Yojimbo target, stream 5 or 4), 235 (stream 17), 243 (stream 18)
and 251 (Magus, stream 18 then 4), 249 is a dead pick. 232 to 234 and 238 are Yojimbo's gil, hire word
and Zanmato tier (SUMMONS.md 5). 236 and 237 read and add items. 246 to 248 are a 16 entry value queue,
250 and 252 the forced roll id, 253 and 254 the command range bytes at +0x714.

### 4.3 Motion, unit events and busy state (24 rows, 6,578 calls)

PlayMotion (7, 111, 163), the wait 8 and the tests 43 and 88. Unit events run battle event scripts for a unit
and wait on the thread (75, 110, 117, and 80, which reloads the units first). The busy flag m_b432 is
set by 49, cleared by 18, and waited on by 76, 256 and the battle idle wait 62. 158 fires a motion
event bit, which is how a hit lands mid animation. 121 reloads the character record, 176 computes the
attack frames, 215 plays the link reaction.

### 4.4 Movement, facing and geometry (57 rows, 6,700 calls)

Three move builders: the eight argument StartMove (12, 83, 133), the arc move (44, 118 with the m_b420
mode of 28 and 29) and the path move (70 to 73). Turning (3, 6, 87, 264, 274, 277), home facing (50,
127, 143, 159, 195, 263), floating and height (9, 10, 41, 265, 146, 192), attach and snap to a bone (82,
84). The rest are distance, angle and position reads between units or masks.

### 4.5 Effects, visibility and the CHR (48 rows, 1,148 calls)

Two counted effect kinds: kind 7 counted in m_bE12 (23, 211, waited by 81 and 188, read by 209) and
kind 8 in m_bE13 (98, 212, waited by 99 and 189, read by 210). Others: entrance effect 64, status
effects 96 and 97, alpha fades 54 and 148, scale 40, part hiding 107, 128, 216, 217, weapon attach 115,
116, 291, 293, and four rows that reach into the CHR the Ch library also exposes: 259 look at a point
(ch:123), 272, 273 and 290 the head sway (ch:118), 285 the blob shadow parameter (ch:133), 191 the
attach scale.

### 4.6 Sound and voice (16 rows, 4,713 calls)

The voiced line rows 100 to 104 and 109, the unit sound load wait 124, and the sound effect rows 108,
123, 135 to 137 and 200 to 203. 108 and 136 keep the last played handle in a one slot global and skip a
repeat.

### 4.7 Messages (11 rows, 3,203 calls)

Show (66, 114, 183, and 287 to 289 on the second table), wait (67, 169), close (68), and the two name
copies 145 and 220.

### 4.8 Battle flow and globals (76 rows, 25,542 calls)

The scripted battle request 2, forced result 52 and 53, the CTB hold events 60, 61 and 151, the event
fire 63, the advantage 34 and 218, escape 166 and 184, the script mode 275 and 276, the system motion
group 162 and 164, and the encounter zone rows 138 to 140. The rest are byte and word globals of the
battle block at 0x112C8xx to 0x112CAxx and per unit bytes, most of them read by one engine function.
Section 7 lists the ones whose purpose is still open.

### 4.9 Debug and no ops (9 rows, 20,970 calls)

The debug prints 21, 95 and 177 (20,814 of the calls are 21), the debug switch 125 and its getter 126,
`g_ffxBtlDbgFullSet` 257, the no ops 65 and 77, and the dead 19.

## 5. Lockstep hazards

The flags in the table: `rng N` (a draw on battle stream N), `input` (a pad read), `sound` (waits on or
returns the state of the local audio device), `booster` (a per machine booster setting), `local` (the
language or another per machine value), `load` (waits on a disk read), `global` (a one slot engine
global, or a hard coded battle, map or magic id case), `render` (the render job), `debug` (a debug
switch). A trailing `*` means the flag came from the call graph or a callee, not the handler body.
There is no `clock` flag: the one wall clock hit is a false path (5.1).

The co-op rule for battle is that actions are replicated and input is not (COOP_DESIGN.md, the battle
section). So the question for each row is whether both peers, running the same script on the same
step, get the same answer. A seeded stream passes that test. A pad, the audio device, the booster and
the language do not.

### 5.1 Random draws [V]

Direct draws:

| row | stream | where | calls | condition |
|---|---|---|---|---|
| 16 AiPickLiveTarget | 4 | third `FFX_BtlAi_FilterAndPickTarget 0x7ACD40` pass, the only one with an out pointer | 917 | more than one unit survives the filter |
| 37 AiPickTarget | 4 | FilterAndPickTarget | 2 | same |
| 105 PickUnitStream7 | 7 | FilterAndPickTarget with stream 7 | 540 | same |
| 231 YojimboPickTargets | 5 or 4 | `sub_7996D0`: `sub_799910` by group on stream 5, or FilterAndPickTarget on 4, by the ability flag | 7 | a non empty group |
| 235 RandStream17 | 17 | `FFX_Rand_Stream(17)` at 0x7A7D12 | 2 | always. Stream 17 is also Yojimbo's per turn roll (SUMMONS.md 5.2) |
| 243 RandPercent | 18 | `FFX_Btl_RandPercentCheck 0x7A8AD0` | 14 | unless the forced id and `word_1136410` are both set |
| 249 PickWithFilter | 4 | `sub_7A8F60` | 0 | dead |
| 251 MagusPickTarget | 18, then 4 | RandPercentCheck, then `sub_7A8F60` | 54 | the 4 draw only when the roll passes |

Through the hit and kill code:

- **btl:14 ApplyPendingHits** runs `FFX_Btl_ApplyPendingEffects 0x78F060` per selected unit, which
  draws stream 9 for some reactions (BATTLE_CTB_AND_STATUS.md 3.2 row 11), and a kill goes through
  `FFX_Btl_OnUnitDefeated` to `FFX_Btl_PayKillRewards 0x7990D0` and its draws (BATTLE_START_AND_END.md
  4.2).
- **btl:158 FireMotionEvent** calls `FFX_Btl_OnMotionEvent 0x7B74D0`, whose hit bits reach the same two.
- **btl:24 and 171** set properties through `FFX_BtlAi_SetProperty 0x7B4B70`, which BATTLE_START_AND_END.md
  4.2 lists as an `OnUnitDefeated(a3 == 0)` caller, so a script that sets HP to 0 pays the kill.

All of these are seeded streams that the simulation already shares, so they replicate if the peers
agree on the seed and take the same script path. The draw counts depend on state that is not on
screen (the survivors of a filter, a reaction kind), as BATTLE_CTB_AND_STATUS.md 3.2 already warns.

False paths, checked at the call site:

- **Stream 16, 19 rows** (23, 47, 54, 57, 63, 64, 96, 98, 121, 130, 131, 144, 148, 211, 212, 229, 230,
  278, 295). Each reaches `sub_7B7170 0x7B7170`, the reel shuffle, through the effect registry
  `sub_7FC2E0` -> `sub_7FD7F0` -> `sub_7FEBA0`. The shuffle draws only when its third argument is
  set, and `sub_7FEBA0` passes `push 0` at 0x7FEC53 for the call at 0x7FEC57. The only other code
  caller, `sub_7AFE00` at 0x7B02E9, is not reachable from any Btl handler (a reverse walk of eight
  levels from it finds none).
- **btl:80** reaches `FFX_Btl_DamageFormula` through `FFX_Btl_LoadUnitParams` and
  `FFX_Btl_ComputeOverdriveRefDamage 0x78D790`, which passes the random flag 0 in both calls (0x78D79C,
  0x78D7B7). btl:214 passes 0 too.
- **btl:164 and the clock.** `sub_79CCA0` frees the system motion group, and the free can reach
  `FFX_Dl_InitMparams 0x6EA050` (dlmalloc `init_mparams`), which seeds the mspace magic from
  `GetTickCount` only while it is still 0 (`MEMORY_ARENAS.md` 2.4). Not a simulation value. The same path as the Ch
  library's ch:61.

### 5.2 Input: the message waits [V]

`sub_8B8530 0x8B8530` returns 1 when the window's text phase is 3 and `test byte ptr
g_ffxMenuPadHeldTrig+2, 20h` (0x8B8543) sees the confirm button newly pressed. `g_ffxMenuPadHeldTrig`
is filled from pad port 0 (CUTSCENE.md). Two Btl polls call it:

- **btl:67 WaitMessageConfirm** (0x7A6A40, 295 calls in 70 battle fields) waits for the press, then
  closes the window.
- **btl:169 WaitMessageOrVoice** (0x7A6CB0, 836 calls) closes on the press, or by itself once the text
  has finished drawing and `sub_8864B0` says no voice is busy.

The battle script thread that owns the window resumes on the step the local player presses. Two peers
press on different steps, so the script continues on different steps, and every later script effect is
shifted. Because battle replicates actions and not input, nothing carries this press today. A port has
to send the host's confirm edge for these polls, or gate them on an agreed step.

### 5.3 Sound pacing: the voiced line [V]

About 830 to 850 calls each, in the same 76 to 80 battle fields, form one pattern per voiced line:
btl:100 PrepareVoice, 114 ShowSubtitle, 101 StartVoice, 222 GetConfigBit9UnlessFfwd, 169
WaitMessageOrVoice, 103 IsVoiceBusy, 104 WaitVoiceDone, 102 StopVoice.

- The polls of 100 and 101 wait for `sub_7B2C10`, the asset loader and the sound streamer.
- 104 waits until `sub_7B2BE0` (voice status `sub_8864B0` or `sub_79D050`) reads 0, and 103 returns that
  status into the script as a value.
- 109's poll waits for `sub_887900`, 124 waits for the unit's sound loads.
- 169 closes when the voice ends, as above.

The voice runs on the audio device's clock, not the step clock, and its load on the disk, so two
machines finish a line on different steps. [I] for the timing, the reads are [V]. A port should
drive these polls from a step count fixed per line, or from the host.

### 5.4 The booster: btl:222 [V]

`FFX_Btl_GetOptionBit9UnlessFfwd 0x785210` (formerly `FFX_Btl_Script_Btl222_GetValue`):

```
785210 cmp  g_boosterSpeedBattleFfwdExported, 0
785217 jz   78521C
785219 xor  eax, eax
78521B retn
78521C mov  eax, g_ffxSaveDataOptionFlags
785221 sar  eax, 9
785224 and  eax, 1
785227 retn
```

So 0 while fast forward is on, else save option bit 9. A machine that is fast forwarding
battles gets 0 where another gets the save bit, and the script branches on it once per voiced line.
Which option bit 9 is was not recovered (the function comment says the same). Either way it is a per
machine value read into battle script control flow.

The kill path also touches a booster: `FFX_Btl_OnUnitDefeated` can call `sub_7871F0`, which clears
`g_boosterAutoBattle` and looks up text line 893 when auto battle is on and a battle is pending or
active. Reached from 14, 24, 158 and 171. It writes no BTLUNIT field itself, but the auto battle
switch it clears is per machine (BATTLE_COMMAND.md).

### 5.5 Language and other machine local values [V]

- **Kill rewards.** `FFX_Btl_PayKillRewards` calls `j_FFX_GetLanguage` at 0x799213 and 0x799317 and,
  on language 11, gives item 0x2048 x4 (bribe) or 0x2032 (drop) where another language gives 0x2065.
  Any kill a script causes reaches this (14, 24, 158, 171). BATTLE_START_AND_END.md 4.2 has the detail.
  Two peers on different languages put different items in the spoils.
- **btl:125 key 0** runs `FFX_Debug_FullItem`, which has the same language 11 swap.
- **Message layout.** 66, 114, 183 and 287 to 289 read the language's message table, with the half
  picked by `FFX_SaveData_GetMapNameAltFlag` (save data, shared). `sub_89B140` moves one window on
  event 389 for languages 2 to 5. Presentation only, except that 169 closes when the text has finished
  drawing, and how long that takes depends on the text, so on the language. [I]
- **btl:220** copies 40 bytes of message text into the unit's `m_battleName`. A hash over BTLUNIT
  differs between languages there.
- **btl:80** reaches `FFX_GetInternationalEnvId` in `FFX_Btl_LoadUnitParams`, which discards the result.
  False path.

### 5.6 One slot globals and hard coded cases [V]

Never reset, so they outlive the battle:

- `word_1136410`, written only by btl:250, read by 252, 253 and `FFX_Btl_RandPercentCheck`. While set,
  btl:243 and 251 calls with a forced id compare instead of drawing. A peer that never ran the battle
  that set it draws where the other compares, and the stream 18 counts part.
- `dword_11363BC`, written only by btl:100 (`id == 0x41FF0004`), read by 272 and 273, which then
  turn selector 5 into selector 0.

Reset at battle begin, but one slot:

- `dword_112C8E0` (108 and `sub_79FD40`) and `dword_112C8E4` (136, 203, `sub_7A0100`) remember the last
  sound handle and skip a repeat.
- `dword_112C8D0` (255), read by `FFX_Btl_QueueDeltaAttack`.
- The 9 entry once list `dword_11363C0` behind btl:123 id 9, cleared by btl:57.
- `dword_11363B8`, set by the btl:24 hack and read by `FFX_Btl_SetUnitShowVote`.

Hard coded:

- btl:24 on battle 0xD40000 with level hash 8207 and prop 84 or 237.
- btl:30 on Magic 146 with battles 0x235, 0x236, 0x238.
- btl:7, 111 and 163 map motion kinds through `sub_7A8C80`, which special cases Magic 434 and 610.
  `MagicFile__getMagicId` is the running effect DLL, simulation state.

### 5.7 Disk, debug and render

- **Disk [V].** 162 waits for the system motion group read through the asset loader, 100 and 101 for
  the sound streamer, 124 for unit sound loads. Disk speed decides the step they finish on.
- **Debug [V].** btl:125 writes the battle debug bytes, including `g_ffxBtlDbgMonInput` (key 3), which
  makes btl:11 refuse to stage and the AI units open the player menu (COOP_DESIGN.md), and
  `g_ffxBtlForcedAdvantage` (key 25). testbattle and two shipped fields (bsil07_51, mihn00_50) call it.
  Script driven, so it replicates. The other writers of the block are the debug window and the bulk
  reset `sub_7CE450` (CHEAT_BATTLE.md 9), and when that reset runs was not checked.
- **Render [V].** 272, 273 and 290 switch the CRT `rand()` head sway on the render job, the hazard the
  Ch library describes in its 5.1.

## 6. The apply script

Applied to the live IDB on 2026-10-26: 259 slots renamed, plus by hand `FFX_Mot_IsFinished` ->
`FFX_Mot_IsRequestActive`, `FFX_Btl_Script_Btl222_GetValue` -> `FFX_Btl_GetOptionBit9UnlessFfwd`, the two
`__RTC_NumErrors_80x` tails -> `FFX_AtelSys_Btl_109_poll_ret5` and `_162_poll_ret5`, btl:111's prototype, the
library comment, and `g_ffxAeonTransitionEffects` moved to 0xC438A8. Live result: 269 table slots named, 46
positional, 0 placeholders.

`../tools/apply_btl_names.py`, modelled on `apply_ch_names.py`. It reads the table out of the IDB
(0xC42628, 296 rows) and names each slot `FFX_AtelSys_Btl_NNN_<Role>_<slot>`. Its `NAMES` block holds
286 rows, 240 used and 46 dead. The other ten rows (11, 15, 16, 19, 24, 37, 90, 170, 171, 279) keep their
descriptive names. It refuses any slot whose current name is not positional for that row
(`FFX_AtelSys_Btl_NNN_<slot>` with or without the zero padding, or `FFX_AtelSys_Btl_NNN` on a single
slot row), a `sub_`/`loc_`/`nullsub_` placeholder, or the known misname `_wcstoul` on 111's getter. It
skips a slot that is not a function start, a function shared between slots and a name already taken,
leaves dead rows positional unless `--include-dead`, and never saves. Dry run by default.

Dry run on the scratch copy, 2026-10-26: 259 slots to rename, 0 skipped. With `--include-dead`: 305 to
rename, 0 skipped. Every slot target is a function start, none is shared, and none of the new names is
taken. 305 is every positional slot in the table, so after an `--include-dead` apply no positional Btl
name is left.

## 7. Open

- Which save option bit 9 is (btl:222). It decides what the voiced line branch does on a machine that
  is not fast forwarding.
- The roles that name a byte rather than a purpose, because no reader that gives the purpose was found:
  0, 1, 4, 17, 20, 22, 31, 38, 39, 42, 48, 150, 153, 160, 179, 181, 208, 221, 266 to 268, 269, 280, 281,
  283, 284, 286, 292. 153 is the fourth heaviest row (5,976 calls) and its byte is read only by
  `sub_780CE0`, which feeds the menu module 17 call.
- btl:5 GetFieldAirborne and btl:41 SetAirborne are read off their readers (the ground height skips and
  the floating entrance effects), not off a field that is known to be the underwater flag. [I]
- btl:101 StartVoice and 102 StopVoice are named from the sound calls `sub_887630` and `sub_887780`,
  which were not traced into the sound driver. [I]
- Stream 7 (btl:105, 540 calls). No other drawer of stream 7 is named in the battle notes.
- Which keys the two shipped fields pass to btl:125. The census shows keys 0, 9, 10, 11, 14, 15, 16, 21,
  22 and 25 as literals in the most common call contexts, and none with key 3, but the key is a
  variable in some of them.
- When `sub_7CE450`, the bulk reset of the debug bytes btl:125 writes, runs. If it is not at battle
  begin, a peer that missed a btl:125 call keeps the old debug values.
## 8. The table

Columns: btl index, call sites, files, src (e event packages, m monster AI, b battle field files, s
system_01), args in push order (the last is the top of the stack), result, blk (yes waits in a poll),
name (the proposed name per slot, `kept` for a slot that keeps its current name, `dead` for an unused
row), meaning, lockstep, mark.

| btl | uses | files | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 394 | 126 | mbs |  | int 0 | no | `FFX_AtelSys_Btl_000_ClearSelfByteDF2_resi` | self m_bDF2 = 0. sub_7AB360 sets m_bDF2 when it starts a unit event with a3 set |  | V |
| 1 | 807 | 807 | b | on:int | int 0 | no | `FFX_AtelSys_Btl_001_SetBtlByteBDED_resi` | low byte of word_112BDED = on & 1. Reset by FFX_Btl_BeginBattle, no other reader found. 807 calls in 807 battle field files, one each |  | I |
| 2 | 493 | 89 | e | battleId:int, returnMode:int | int | no | `FFX_AtelSys_Btl_002_RequestScriptedBattle_start`, `FFX_AtelSys_Btl_002_RequestScriptedBattle_poll` | start calls FFX_Btl_RequestScriptedBattle(battleId, 1, returnMode). The poll returns 1, done without the continue bit, so the next instruction runs on the next step |  | V |
| 3 | 207 | 62 | ms | rampFrames:int, holdFrames:int | int 0 | no | `FFX_AtelSys_Btl_003_StartTurnKind2_resi` | sub_795EB0(self, 2, 0, -1, ramp, hold): a direction change of kind 2, total m_b42E = hold + 2 * ramp frames. Refused while m_directionFixFlag |  | V |
| 4 | 0 | 0 |  | v:float | int 0 | no | dead `FFX_AtelSys_Btl_004_SetBtlFloatBDE4_resi` | unk_112BDE4 = v, read by sub_793BA0 |  | V |
| 5 | 5378 | 542 | bs |  | int byte_112C9CE | no | `FFX_AtelSys_Btl_005_GetFieldAirborne_resi` | byte_112C9CE, copied from byte 3 of the battle field monster section by FFX_Btl_SetupFieldPointers. When set the unit position code skips the ground height snaps and the entrance effects take the floating variant, so a whole field of floating units. The reading is from its 20 readers |  | I |
| 6 | 358 | 71 | ms | rampFrames:int, holdFrames:int | int 0 | no | `FFX_AtelSys_Btl_006_StartTurnKind1IfFx_resi` | as btl:3 with kind 1, only when self m_directionChangeEffect is set |  | V |
| 7 | 2188 | 129 | mbs | motion:int | int frames << 8 / speed | no | `FFX_AtelSys_Btl_007_PlayMotion_resi` | sub_7A8FC0(.., 0): maps the motion kind through sub_7A8C80 (the unit's form bytes +0x408/+0x409, Magic 434 and 610 special case), FFX_Mot_SetByModeIndex, blend frames unless bit 0x8000. Returns the length scaled by the motion speed, 1 when nothing played |  | V |
| 8 | 1444 | 125 | mbs |  | none | yes | `FFX_AtelSys_Btl_008_WaitMotion_poll` | poll only. 0 while self m_b3F3 is set, 5 with no unit or no CHR, else sub_7A8C50: 0 while the CHR motion request (+0x736 m_motionRequestActive, read by the misnamed FFX_Mot_IsFinished) is active and BTLUNIT +0x434 is clear, 5 otherwise |  | V |
| 9 | 128 | 103 | ms | on:int | int 0 | no | `FFX_AtelSys_Btl_009_SetFloating_resi` | FFX_Btl_SetUnitGroundMode(self, on ? 0 : 1) |  | V |
| 10 | 101 | 67 | ms | mode:int, base:float | int 0 | no | `FFX_AtelSys_Btl_010_SetYOffset_resi` | self m_yOffsetBase = base, m_b50C = mode, then sub_7AB300 recomputes m_yOffset (mode 0 centres on flt_112BDE8 minus half the height, mode 1 zero) |  | V |
| 11 | 1622 | 298 | ms | sel:int, ability:int | int 1 staged, 0 refused | no | kept `FFX_AtelSys_Btl_011_AiAddCommand_resi` | Refuses when sub_7B24A0 says the unit cannot act or g_ffxBtlDbgMonInput is set, then FFX_BtlAi_AddCommandEntry(self, ability, mask, 0, -1) | global | V |
| 12 | 407 | 73 | mbs | mode:int, speed:float, frames:int, i1:int, i2:int, x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Btl_012_StartMove_resi` | sub_7A9150(.., 0.0): pops eight values and calls FFX_Btl_StartUnitMove on self, nothing when frames <= 0 |  | V |
| 13 | 44 | 34 | ms | targetKind:int, sectors:int | int sector | no | `FFX_AtelSys_Btl_013_GetTargetDirSector_resi` | resolves the move target point of kind targetKind (sub_7A9AC0) and returns sub_7A87F0, the direction to it quantised to one of sectors. A negative count with m_b44E set returns m_b44E < 0 |  | V |
| 14 | 1 | 1 | s | sel:int | int 0 | no | `FFX_AtelSys_Btl_014_ApplyPendingHits_resi` | sub_78D980(self, sel, 0): FFX_Btl_ApplyPendingEffects on every selected unit, sub_78EE80 on the rest. With no hit landed and the self KO flag it kills self through FFX_Btl_OnUnitDefeated. Only system_01 calls it, once | rng 9, rng 10-13*, local*, booster* | V |
| 15 | 11468 | 807 | mbs | sel:int, prop:int | int value | no | kept `FFX_AtelSys_Btl_015_GetProperty_resi` | FFX_BtlAi_GetProperty(self, prop, sel) |  | V |
| 16 | 917 | 285 | mbs | sel:int, prop:int, value:int, flags:int | int unit | no | kept `FFX_AtelSys_Btl_016_AiPickLiveTarget_resi` | Three FilterAndPickTarget passes, the first two with no out pointer, the third draws stream 4 when more than one unit survives | rng 4 | V |
| 17 | 0 | 0 |  | on:int | int 0 | no | dead `FFX_AtelSys_Btl_017_SetBtlByteA8FC_resi` | byte_112A8FC = on & 1, read by FFX_Btl_MainStep and the battle debug window | debug | V |
| 18 | 65 | 21 | ms |  | int 0 | no | `FFX_AtelSys_Btl_018_ClearBusyFlags_resi` | self m_b435 = 0 and m_b432 = 0, except when m_bDFB is 2, which moves m_bDFB to 3 and keeps m_b432 |  | V |
| 19 | 0 | 0 |  | speed:float | int 0 | no | kept `FFX_BtlAtelOp_SetUnitMoveSpeed` | FFX_Ch_SetMoveSpeed on the self CHR. The name is right |  | V |
| 20 | 353 | 86 | mbs |  | int dword_112C8D8 | no | `FFX_AtelSys_Btl_020_GetCommandExecWord_resi` | dword_112C8D8, written by FFX_Btl_BeginBattle and FFX_Btl_StartCommandExec, read by FFX_Btl_CtbTick |  | I |
| 21 | 20814 | 569 | embs | v:int | int 0 | no | `FFX_AtelSys_Btl_021_DebugPrintInt_resi` | pops one value into a stubbed debug print. The second heaviest row, 20,814 calls |  | V |
| 22 | 566 | 146 | mbs | v:int | int 0 | no | `FFX_AtelSys_Btl_022_SetSelfByte3F2_resi` | unless self m_b3F3 is set it runs sub_7AB6C0 (clears +0x432, +0x43E, +0x404), then m_b3F2 = v |  | V |
| 23 | 67 | 38 | m | id:int, param:int | int 0 | no | `FFX_AtelSys_Btl_023_PlayEffectA_resi` | sub_79EB10(self, id, 7, param, 1) registers a kind 7 effect, and on success ++self m_bE12, which btl:81 waits on |  | V |
| 24 | 32859 | 952 | mbs | sel:int, prop:int, value:int | int | no | kept `FFX_AtelSys_Btl_024_SetProperty_resi` | FFX_BtlAi_SetProperty. Hard coded: on battle 0xD40000 with level hash 8207, prop 84 or 237 sets dword_11363B8. The heaviest row, 32,859 calls | rng 10-13*, local*, global | V |
| 25 | 264 | 97 | mb |  | int ability | no | `FFX_AtelSys_Btl_025_AiGetCurrentCommand_resi` | FFX_BtlAi_GetCurrentEntryId(self): the ability of the issuer's current command, ids 12404..12407 mapped through the sub_7B19D0 slots |  | V |
| 26 | 87 | 54 | m | ability:int, field:int | int | no | `FFX_AtelSys_Btl_026_GetAbilityField_resi` | sub_78CD30(self, ability, field), an ability record query, field 0..10 |  | V |
| 27 | 5 | 2 | m | sel:int, ability:int | int 0 | no | `FFX_AtelSys_Btl_027_AiOverrideCurrentCommand_resi` | FFX_BtlAi_OverrideCurrentEntry(self, ability, mask): rewrites the issuer's current queued command |  | V |
| 28 | 67 | 24 | m | mode:int | int 0 | no | `FFX_AtelSys_Btl_028_SetMoveArcMode_resi` | self m_b420 = mode, the 0..3 mode btl:118's timeline (sub_7B78C0) switches on |  | V |
| 29 | 100 | 25 | m |  | int m_b420 | no | `FFX_AtelSys_Btl_029_GetMoveArcMode_resi` | self m_b420 |  | V |
| 30 | 2726 | 818 | mbs | selA:int, selB:int | int count | no | `FFX_AtelSys_Btl_030_CountUnitsInBoth_resi` | units in both masks. Hard coded: with Magic 146 running, dword_12FB7E0 == 1 and a zero count on battles 0x235/0x236/0x238 it clears dword_12FB7E0 | global | V |
| 31 | 3 | 3 | m | v:int | int 0 | no | `FFX_AtelSys_Btl_031_SetSelfByte3F1_resi` | self m_b3F1 = v |  | V |
| 32 | 0 | 0 |  |  | int count | no | dead `FFX_AtelSys_Btl_032_CountBusyTargets_resi` | sub_7A92C0(self, m_targetMask): target units other than self with m_b508, or with m_b436, also writes word_112CA08 |  | V |
| 33 | 1343 | 769 | mbs | sel:int | int typeId, -1 none | no | `FFX_AtelSys_Btl_033_GetUnitTypeId_resi` | m_head.m_typeId of the selected unit |  | V |
| 34 | 145 | 136 | mb | v:int | int 0 | no | `FFX_AtelSys_Btl_034_SetAdvantage_resi` | byte 1 of dword_112C9DB, the advantage (0 normal, 1 pre-emptive, 2 ambush). btl:218 reads it |  | V |
| 35 | 56 | 13 | mbs | sel:int | int | no | `FFX_AtelSys_Btl_035_GetAvgDistance_resi` | average over the mask of sub_7A9FC0(unit, 1), the distance from a unit to its kind 1 point less the radii |  | V |
| 36 | 473 | 189 | mbs |  | int (scene << 16) + encounter | no | `FFX_AtelSys_Btl_036_GetBattleId_resi` | sub_782970, 0 outside a battle |  | V |
| 37 | 2 | 1 | m | sel:int, prop:int, value:int, flags:int | int unit | no | kept `FFX_AtelSys_Btl_037_AiPickTarget_resi` | One FilterAndPickTarget pass with an out pointer, stream 4 | rng 4 | V |
| 38 | 240 | 53 | ms | v:int | int 0 | no | `FFX_AtelSys_Btl_038_SetSelfByte44F_resi` | self m_b44F = v |  | V |
| 39 | 18 | 1 | s |  | int | no | `FFX_AtelSys_Btl_039_GetSelfByte44F_resi` | self m_b44F |  | V |
| 40 | 97 | 60 | ms | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Btl_040_SetScale_resi` | self m_scale and FFX_Ch_SetScaleMasked(chr, 7, x, y, z) |  | V |
| 41 | 92 | 74 | ms | on:int | int 0 | no | `FFX_AtelSys_Btl_041_SetAirborne_resi` | self m_b505 = on & 1, the per unit form of btl:5. btl:265 sets it with a height |  | I |
| 42 | 36 | 32 | ms |  | int | no | `FFX_AtelSys_Btl_042_GetSelfByte508_resi` | self m_b508, which btl:62 counts as busy |  | V |
| 43 | 226 | 57 | mbs |  | int -1 or 0 | no | `FFX_AtelSys_Btl_043_IsMotionActive_resi` | -1 while the self CHR motion request +0x736 is active (FFX_Mot_IsFinished, which returns that byte), the Btl form of ch:4 |  | V |
| 44 | 71 | 45 | ms | kind:int, speed:float, speedMax:float, accel:float, yawK:float, arcK:float, f6:float, f7:float, f8:float | int sector | no | `FFX_AtelSys_Btl_044_StartArcMove_resi` | sets the move speeds and kind OR 0x80, solves the frame count from the distance and the acceleration, sets the yaw and pitch ramps, returns the direction sector of 16 |  | V |
| 45 | 176 | 73 | ms |  | int 0 | no | `FFX_AtelSys_Btl_045_ResetMoveState_resi` | sub_7AB730: self +0x415 = 0 and the word at +0x417 = 0 |  | V |
| 46 | 157 | 36 | ms | targetKind:int | float | no | `FFX_AtelSys_Btl_046_GetDistanceTo_resf` | sub_7A9A40(self, kind, 0): distance to the kind's point less the point's own radius |  | V |
| 47 | 6 | 5 | ms | leaveField:int | int 0 | no | `FFX_AtelSys_Btl_047_ResetUnit_resi` | m_b508 = 0, stops the status effects, resets the motion to its idle and the show vote, with leaveField m_onField = 0 |  | V |
| 48 | 36 | 32 | ms |  | int | no | `FFX_AtelSys_Btl_048_GetSelfByte412_resi` | self m_b412 |  | V |
| 49 | 36 | 32 | ms |  | int 0 | no | `FFX_AtelSys_Btl_049_SetBusy_resi` | self m_b432 = 1, the flag btl:76 and btl:62 wait on |  | V |
| 50 | 71 | 24 | mb | sel:int, degrees:float | int 0 | no | `FFX_AtelSys_Btl_050_SetHomeFacing_resi` | for each unit m_b512 = 1 and both m_homeFacing slots = the angle |  | V |
| 51 | 20 | 19 | m | on:int | int 0 | no | `FFX_AtelSys_Btl_051_SetChrFlag80Inv_resi` | self m_head.m_chrFlag80Inv = on & 1 |  | V |
| 52 | 56 | 38 | mb | result:int | int result | no | `FFX_AtelSys_Btl_052_ForceBattleResult_resi` | low byte of g_ffxBtlForcedResult = result, and sub phase 1 moves to 2 (BATTLE_START_AND_END.md) |  | V |
| 53 | 126 | 35 | ebs |  | int | no | `FFX_AtelSys_Btl_053_GetForcedResult_resi` | low byte of g_ffxBtlForcedResult |  | V |
| 54 | 12 | 3 | ms | from:float, to:float, frames:int | int 0 | no | `FFX_AtelSys_Btl_054_FadeAlpha_resi` | sub_796550(self, from, to, frames, 1) |  | V |
| 55 | 115 | 31 | mb | sel:int, ability:int | int count | no | `FFX_AtelSys_Btl_055_AiQueueCommand_resi` | sub_7AC840(self, ability, sel, 1): appends to m_queuedAiCommand (4 deep) of each unit and inserts into the per unit list at dword_2310CD8 |  | V |
| 56 | 52 | 15 | mb | sel:int, ability:int | int count | no | `FFX_AtelSys_Btl_056_AiUnqueueCommand_resi` | sub_7ACB40: removes the ability from both lists |  | V |
| 57 | 50 | 44 | ms |  | int 0 | no | `FFX_AtelSys_Btl_057_ResetActionState_resi` | sub_78CCA0 (+0x3F4 = 1, refresh status fx, sub_788E60) and sub_7A8F20, which clears the 9 entry once list btl:123 uses |  | V |
| 58 | 1 | 1 | m | speed:int | int 0 | no | `FFX_AtelSys_Btl_058_SetMotionSpeed_resi` | self m_motionSpeed |  | V |
| 59 | 1000 | 137 | mb | sel:int, id:int, on:int | int count | no | `FFX_AtelSys_Btl_059_SetAbilitySealed_resi` | sub_79B470 per unit: id 0x3NNN sets or clears its m_sealedAbilities bit, an id with no top nibble writes m_bDD4 of unit id |  | V |
| 60 | 170 | 59 | mbs | event:int | int | yes | `FFX_AtelSys_Btl_060_FireHoldEventA_start`, `FFX_AtelSys_Btl_060_FireHoldEventA_poll` | start fires a slot 62/63 battle event through sub_7981E0 and sets the high byte of g_ffxBtlEndResult to 2 (the CTB hold, BATTLE_CTB_AND_STATUS.md). The poll sub_7A8700 waits for the event thread |  | V |
| 61 | 398 | 139 | bs |  | int 0 | no | `FFX_AtelSys_Btl_061_ReleaseEventHold_resi` | high byte of g_ffxBtlEndResult = 0 |  | V |
| 62 | 645 | 137 | bs |  | none | yes | `FFX_AtelSys_Btl_062_WaitBattleIdle_poll` | poll only. 0 while a poison is pending, the command queue is not empty, three phase bytes are set, or any on field unit has m_b508, m_b432 or m_bDFB, or any unit m_b436 or an action state. Writes the culprit to word_112CA08 |  | V |
| 63 | 4184 | 786 | mbs | group:int, event:int | int | yes | `FFX_AtelSys_Btl_063_FireEventAndWait_start`, `FFX_AtelSys_Btl_063_FireEventAndWait_poll` | start FFX_BtlEv_FireEvent(max(group - 1, 0), 255, event, 0xFFFF, 1, 2, actor), the poll waits for the event thread |  | V |
| 64 | 32 | 7 | ms | mode:int | int | no | `FFX_AtelSys_Btl_064_PlayEntranceEffect_resi` | sub_79E8C0(self, mode): picks effect 32, 33, 52 or 53 by btl:5, m_b505 and m_b5C0 and plays it |  | V |
| 65 | 54 | 15 | ms |  | int 0 | no | `FFX_AtelSys_Btl_065_Nop_resi` | returns 0 |  | V |
| 66 | 271 | 66 | b | window:int, msg:int, x:int, y:int, align:int | int 0 | no | `FFX_AtelSys_Btl_066_ShowMessage_resi` | opens a battle message window (clamped 0..6) on the text at dword_112A9B8, entry msg, half picked by FFX_SaveData_GetMapNameAltFlag. sub_89B140 moves one window on event 389 for languages 2..5 | local | V |
| 67 | 295 | 70 | b | window:int | int | yes | `FFX_AtelSys_Btl_067_WaitMessageConfirm_start`, `FFX_AtelSys_Btl_067_WaitMessageConfirm_poll` | start clamps the window. The poll returns 0 until the text phase is 3 and the confirm bit of g_ffxMenuPadHeldTrig is newly pressed (sub_8B8530), then closes the window | input | V |
| 68 | 927 | 105 | b | window:int | int 0 | no | `FFX_AtelSys_Btl_068_CloseMessage_resi` | FFX_MesWin_Deactivate |  | V |
| 69 | 44 | 30 | b | sel:int | int | no | `FFX_AtelSys_Btl_069_GetApproachDistance_resi` | sub_7A9FC0(unit, 12) / 2.5 |  | V |
| 70 | 15 | 7 | mb | slot:int | int 0 | no | `FFX_AtelSys_Btl_070_ClearPath_resi` | sub_7AB140: dword_1136418[slot] = 0, slots 0..2 |  | V |
| 71 | 60 | 7 | mb | slot:int, kind:int | int 0 | no | `FFX_AtelSys_Btl_071_AddPathPointKind_resi` | sub_7AB0B0: appends the self point of kind to path slot (32 deep) |  | V |
| 72 | 315 | 7 | mb | slot:int, x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Btl_072_AddPathPoint_resi` | sub_7AB0B0 with an explicit point |  | V |
| 73 | 15 | 7 | mb | slot:int, frames:int, yaw:float, flag:int | int 0 | no | `FFX_AtelSys_Btl_073_StartPathMove_resi` | sub_7AB010: move kind 7 along the path for frames |  | V |
| 74 | 184 | 56 | mbs | sel:int | int count | no | `FFX_AtelSys_Btl_074_CountMovingUnits_resi` | units in the mask with m_moveKind != 0 |  | V |
| 75 | 252 | 33 | mbs | sel:int, event:int, flag:int | int | yes | `FFX_AtelSys_Btl_075_RunUnitEventAndWait_start`, `FFX_AtelSys_Btl_075_RunUnitEventAndWait_poll` | start runs sub_7AB360 on every unit, the unit's battle event (enemy groups 3/64/4, party 5 + unit), poll waits for the thread |  | V |
| 76 | 104 | 21 | bs | sel:int | none | yes | `FFX_AtelSys_Btl_076_WaitUnitsIdle_start`, `FFX_AtelSys_Btl_076_WaitUnitsIdle_poll` | start resolves the mask, poll waits until no unit in it has m_b432 |  | V |
| 77 | 0 | 0 |  |  | int 0 | no | dead `FFX_AtelSys_Btl_077_Nop_resi` | returns 0 |  | V |
| 78 | 0 | 0 |  | v:int | int 0 | no | dead `FFX_AtelSys_Btl_078_SetLeaveOnDeath_resi` | self m_leaveOnDeath and m_leaveOnDeathDefault |  | V |
| 79 | 0 | 0 |  | sel:int, v:int | int 0 | no | dead `FFX_AtelSys_Btl_079_SetByteDDD_resi` | m_bDDD = v for each unit |  | V |
| 80 | 37 | 14 | mb | sel:int | int | yes | `FFX_AtelSys_Btl_080_ReloadUnitsAndWait_start`, `FFX_AtelSys_Btl_080_ReloadUnitsAndWait_poll` | start runs sub_7A89C0: FFX_Btl_LoadUnitParams, the re-entry reset, CTB 3 x tick, shade reset, status fx and motion 39 per unit (BATTLE_CTB_AND_STATUS.md). The poll waits for the motion event |  | V |
| 81 | 19 | 13 | m |  | none | yes | `FFX_AtelSys_Btl_081_WaitEffectA_poll` | poll only, waits for self m_bE12 == 0 |  | V |
| 82 | 30 | 11 | m | sel:int, parentSel:int, bone:int | int 0 | no | `FFX_AtelSys_Btl_082_AttachToUnit_resi` | +0x43F = parent, +0x441 = bone, clears show vote 0, places the unit, ++parent m_hasAttachedUnits |  | V |
| 83 | 2 | 2 | m | mode:int, speed:float, frames:int, i1:int, i2:int, x:float, y:float, z:float, arg:float | int 0 | no | `FFX_AtelSys_Btl_083_StartMoveArg_resi` | btl:12 with the extra float as sub_7A9150's a4 |  | V |
| 84 | 3 | 1 | m | sel:int, otherSel:int, bone:int | int 0 | no | `FFX_AtelSys_Btl_084_SnapToBone_resi` | the unit's position = the bone world position of the other unit's CHR |  | V |
| 85 | 45 | 35 | mb | on:int | int 0 | no | `FFX_AtelSys_Btl_085_SetShowVote0_resi` | FFX_Btl_SetFlag100Vote(self, 0, on & 1) |  | V |
| 86 | 4 | 4 | b | mode:int | int 0 | no | `FFX_AtelSys_Btl_086_SetSpecialBlock_resi` | sub_79DE60(mode): mode 1 allocates a 0x870 byte block into dword_11360A4 |  | I |
| 87 | 38 | 16 | mbs | kind:int, f:float, rampFrames:int, holdFrames:int | int 0 | no | `FFX_AtelSys_Btl_087_StartTurnKind_resi` | as btl:3 with the kind from the script, the float is popped and dropped |  | V |
| 88 | 175 | 33 | ms | sel:int, kind:int | int 0 yes, -1 no | no | `FFX_AtelSys_Btl_088_IsPlayingMotionKind_resi` | compares the unit's current motion index +0x3F0 with sub_7A8C80(unit, kind) |  | V |
| 89 | 66 | 45 | ms | targetKind:int | float | no | `FFX_AtelSys_Btl_089_GetDistanceToFlat_resf` | btl:46 with the height ignored |  | V |
| 90 | 68 | 28 | m | sel:int, ability:int | int 1 | no | kept `FFX_AtelSys_Btl_090_AiAddCommandNoReorder_resi` | FFX_BtlAi_AddCommandEntry with noReorder 1 |  | V |
| 91 | 64 | 7 | bs | sel:int | float | no | `FFX_AtelSys_Btl_091_GetUnitDepth_resf` | sub_794A70: the CHR depth extent times the z scale |  | I |
| 92 | 72 | 8 | bs | sel:int | float | no | `FFX_AtelSys_Btl_092_GetUnitHeight_resf` | FFX_Ch_GetF4F4(chr) * m_scale[1] |  | V |
| 93 | 186 | 172 | m | a:int, b:int | int unit | no | `FFX_AtelSys_Btl_093_RequestShowFx_resi` | pops two and drops them, self m_head.m_pendingShowFx = 1 |  | V |
| 94 | 0 | 0 |  |  | int 0 | no | dead `FFX_AtelSys_Btl_094_ResetChrState_resi` | sub_788E60: m_head.m_b1F = 0, FFX_Ch_SetI408(chr, 0), sub_7FBCD0 |  | V |
| 95 | 9 | 1 | b | v:float | int 0 | no | `FFX_AtelSys_Btl_095_DebugPrintFloat_resi` | pops one float into a stubbed print |  | V |
| 96 | 0 | 0 |  |  | int 0 | no | dead `FFX_AtelSys_Btl_096_RefreshStatusFx_resi` | sub_79EFF0(self), re-registers the status effects |  | V |
| 97 | 1 | 1 | s |  | int 0 | no | `FFX_AtelSys_Btl_097_StopStatusFx_resi` | FFX_Btl_StopStatusFx(self) |  | V |
| 98 | 16 | 11 | m | id:int, param:int | int | no | `FFX_AtelSys_Btl_098_PlayEffectB_resi` | sub_79EBA0(self, id, param), the kind 8 twin of btl:23, counted in m_bE13 |  | V |
| 99 | 1 | 1 | m |  | none | yes | `FFX_AtelSys_Btl_099_WaitEffectB_poll` | poll only, waits for self m_bE13 == 0 |  | V |
| 100 | 853 | 80 | bs | id:int | int 0 | yes | `FFX_AtelSys_Btl_100_PrepareVoice_start`, `FFX_AtelSys_Btl_100_PrepareVoice_poll`, `FFX_AtelSys_Btl_100_PrepareVoice_resi` | start stores id in dword_112C24C, the poll waits for a sound phase (sub_7A8C20) and the sound loader (sub_7B2C10), the getter sets dword_11363BC = (id == 0x41FF0004) and loads the voice with sub_887570. Nothing resets dword_11363BC | load, sound, global | V |
| 101 | 853 | 80 | bs |  | int 0 | yes | `FFX_AtelSys_Btl_101_StartVoice_poll`, `FFX_AtelSys_Btl_101_StartVoice_resi` | poll waits like btl:100, the getter sub_887630 starts the prepared voice at volume 63/127 | load, sound | I |
| 102 | 839 | 76 | b |  | int 0 | no | `FFX_AtelSys_Btl_102_StopVoice_resi` | sub_7B2C00 -> sub_887780 |  | I |
| 103 | 836 | 76 | b |  | int 0 or 1 | no | `FFX_AtelSys_Btl_103_IsVoiceBusy_resi` | sub_7B2BE0: the voice status word or the sound status | sound | V |
| 104 | 851 | 79 | bs |  | none | yes | `FFX_AtelSys_Btl_104_WaitVoiceDone_poll` | poll only, returns 1 once sub_7B2BE0 is 0, else 0 | sound | V |
| 105 | 540 | 536 | bs | sel:int, prop:int, value:int, flags:int | int unit | no | `FFX_AtelSys_Btl_105_PickUnitStream7_resi` | FilterAndPickTarget(unit 0, .., stream 7, out). Draws stream 7 when more than one survives | rng 7 | V |
| 106 | 0 | 0 |  | sel:int | int 0 yes, -1 no | no | dead `FFX_AtelSys_Btl_106_IsSoleTarget_resi` | 0 when the unit's m_targetMask is exactly itself |  | V |
| 107 | 124 | 16 | mb | sel:int, part:int, on:int | int 0 | no | `FFX_AtelSys_Btl_107_SetUnitPartHidden_resi` | FFX_Ch_ModelSetHide(chr, part, on & 1) |  | V |
| 108 | 0 | 0 |  | sel:int, index:int | int 0 | no | dead `FFX_AtelSys_Btl_108_PlayUnitSound_resi` | plays m_soundHandles[index] of the unit's primary through sub_7A0A30 unless it equals dword_112C8E0, the last played handle | global | V |
| 109 | 8 | 1 | s | sel:int, index:int | int | yes | `FFX_AtelSys_Btl_109_PlayUnitCue_start`, `FFX_AtelSys_Btl_109_PlayUnitCue_poll`, `FFX_AtelSys_Btl_109_PlayUnitCue_resi` | start stores both, the poll waits for the sound idle (sub_887900), the getter plays byte_C42510[8 * unit + index] through sub_8875D0. Skipped on a btl:5 field | sound | V |
| 110 | 66 | 18 | ms | sel:int, event:int | int | yes | `FFX_AtelSys_Btl_110_RunUnitAltEventAndWait_start`, `FFX_AtelSys_Btl_110_RunUnitAltEventAndWait_poll` | start runs sub_7A0720 per unit (battle events 79 + unit and 77/78), poll waits |  | V |
| 111 | 58 | 40 | ms | motion:int | int frames | no | `FFX_AtelSys_Btl_111_PlayMotionFlagged_resi` | btl:7 with +0x40A = 1. The IDB prototype is still the wcstoul one |  | V |
| 112 | 210 | 76 | b |  | int 0 | no | `FFX_AtelSys_Btl_112_SetMenuFlagOn_resi` | sub_89BC60(1): byte_C58EF4, read by FFX_BtlMenu_DrawRoot |  | V |
| 113 | 680 | 79 | mb |  | int 0 | no | `FFX_AtelSys_Btl_113_SetMenuFlagOff_resi` | sub_89BC60(0) |  | V |
| 114 | 832 | 76 | b | window:int, msg:int | int 0 | no | `FFX_AtelSys_Btl_114_ShowSubtitle_resi` | the btl:66 text with a fixed layout (sub_89B230, flags 0x2002). The voiced line pattern pairs it with 100 to 104, 169 and 222 | local | V |
| 115 | 3 | 1 | s | side:int | int | no | `FFX_AtelSys_Btl_115_AttachWeapon_resi` | sub_793330(self, side): parents the side record CHR to its bone on self |  | V |
| 116 | 2 | 1 | s | side:int | int | no | `FFX_AtelSys_Btl_116_DetachWeapon_resi` | sub_793F10: unparents it at the bone's world position |  | V |
| 117 | 6 | 1 | s | sel:int, event:int, flag:int | int | yes | `FFX_AtelSys_Btl_117_RunWeaponEventAndWait_start`, `FFX_AtelSys_Btl_117_RunWeaponEventAndWait_poll` | btl:75 on each unit's weapon stand in unit |  | V |
| 118 | 3 | 1 | s | frames:int, yaw:float, flag:int | int 0 | no | `FFX_AtelSys_Btl_118_StartArcTimeline_resi` | records the start position, builds an 8 point path with sub_7B78C0 by m_b420 and starts it with sub_7AB010(2, ..) |  | V |
| 119 | 12 | 11 | bs | sel:int, targetKind:int | int | no | `FFX_AtelSys_Btl_119_GetApproachFrames_resi` | min(distance / 20 + 4, 20) |  | V |
| 120 | 19 | 6 | m | sel:int, ability:int, field:int | int | no | `FFX_AtelSys_Btl_120_GetAbilityFieldOf_resi` | btl:26 on another unit |  | V |
| 121 | 1 | 1 | s |  | int 0 | no | `FFX_AtelSys_Btl_121_ReloadCharRecord_resi` | restores the auto statuses, reloads the character record, sub_78D7F0, applies the auto statuses, refreshes the status fx |  | V |
| 122 | 82 | 41 | ms | sel:int | int | no | `FFX_AtelSys_Btl_122_GetHitResult_resi` | sub_79A920(unit, its first target), the hit log entry kind |  | V |
| 123 | 335 | 110 | mbs | sel:int, id:int | int | no | `FFX_AtelSys_Btl_123_PlayUnitSe_resi` | sub_79FD40(unit, id), id 9 only once per unit through the sub_7A8920 list |  | V |
| 124 | 1 | 1 | s |  | none | yes | `FFX_AtelSys_Btl_124_WaitUnitSoundLoaded_poll` | poll only, waits while the primary unit has sound loads issued and m_soundReady is clear | load | V |
| 125 | 41 | 3 | eb | value:int, key:int | int value | no | `FFX_AtelSys_Btl_125_SetDebugParam_resi` | the battle debug switch: 0 FullItem, 1 to 9 and 12 to 20, 23, 24 debug bytes, 3 g_ffxBtlDbgMonInput, 10 sphere levels and gil, 11 fills equipment, 21 FFX_SaveData_GrantAllAbilities, 25 g_ffxBtlForcedAdvantage. Called by testbattle and two shipped fields | debug, local, load | V |
| 126 | 34 | 1 | e | key:int | int | no | `FFX_AtelSys_Btl_126_GetDebugParam_resi` | reads the same bytes back | debug | V |
| 127 | 68 | 26 | mbs | sel:int | int | no | `FFX_AtelSys_Btl_127_ResetToHomePose_resi` | sub_7AB2A0: rotation to the home facing, position to the target point |  | V |
| 128 | 2 | 1 | s | on:int | int 0 | no | `FFX_AtelSys_Btl_128_SwapUnit2Parts_resi` | sub_8720E0 on the unit 2 CHR: hides model part 1 or part 2 |  | V |
| 129 | 1 | 1 | b |  | none | yes | `FFX_AtelSys_Btl_129_WaitFlagC022_poll` | poll only, waits for the low byte of word_112C022 == 1 |  | I |
| 130 | 4 | 4 | m | id:int, param:int | int | no | `FFX_AtelSys_Btl_130_PlayEffectKind1_resi` | sub_79EB10(self, id, 1, param, 1) |  | V |
| 131 | 29 | 18 | ms | unused:int, on:int | int 0 | no | `FFX_AtelSys_Btl_131_AttachChrEffect_resi` | with on > 0, m_bE15 set and none yet, registers an effect on the self CHR (sub_7FC9B0) into m_iEA8 |  | V |
| 132 | 29 | 18 | ms |  | int 0 | no | `FFX_AtelSys_Btl_132_ReleaseChrEffect_resi` | sub_788E30: releases the +0xEA8 effect |  | V |
| 133 | 59 | 40 | ms | mode:int, speed:float, frames:int, i1:int, i2:int, x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Btl_133_StartMoveFlag418_resi` | btl:12, then +0x418 = 1 |  | V |
| 134 | 60 | 18 | ms |  | int 0 | no | `FFX_AtelSys_Btl_134_SpendCommandCost_resi` | FFX_Btl_SpendCommandCost(self) |  | V |
| 135 | 8 | 6 | m | sel:int, id:int, param:int | int | no | `FFX_AtelSys_Btl_135_PlayStatusSe_resi` | sub_79FCF0 -> sub_79FE80 / sub_7A08A0 |  | V |
| 136 | 19 | 7 | mbs | sel:int, id:int | int | no | `FFX_AtelSys_Btl_136_PlayUnitSeOnce_resi` | sub_7A0100: plays id OR 0x80000000 unless it is dword_112C8E4, the last one | global | V |
| 137 | 0 | 0 |  | sel:int, id:int, param:int | int | no | dead `FFX_AtelSys_Btl_137_PlayRawSe_resi` | sub_7A00D0 -> sub_7A08A0(id OR 0x80000000) |  | V |
| 138 | 1 | 1 | e | battleId:int | int 0 | no | `FFX_AtelSys_Btl_138_ResetZoneWeights_resi` | FFX_Btl_ResetBattleMapWeights -> FFX_Btl_ResetEncounterZoneWeights. Only camtest0 calls it |  | V |
| 139 | 1 | 1 | e | battleId:int | int | no | `FFX_AtelSys_Btl_139_GetZoneNibble_resi` | FFX_Btl_GetBattleLiveWeight |  | I |
| 140 | 34 | 6 | e | battleId:int, v:int | int | no | `FFX_AtelSys_Btl_140_SetZoneNibble_resi` | FFX_Btl_SetBattleLiveWeight: sets the zone nibble and resets the weights. Event packages call it |  | I |
| 141 | 1075 | 536 | bs |  | int | no | `FFX_AtelSys_Btl_141_GetWordC9E2Low_resi` | low byte of word_112C9E2 |  | V |
| 142 | 36 | 32 | ms |  | int m_b508 | no | `FFX_AtelSys_Btl_142_TestGroundHold_resi` | sub_7AA100 recomputes m_b508, with status 0 and m_b505 it forces ground mode 1 and +0x4F0 = 10 around the call |  | V |
| 143 | 36 | 32 | ms | sectors:int | int | no | `FFX_AtelSys_Btl_143_GetHomeDirSector_resi` | sub_7A8720(self, sectors, 1): the home facing delta quantised |  | V |
| 144 | 0 | 0 |  | unused:int | int 0 | no | dead `FFX_AtelSys_Btl_144_PlayEffect12_resi` | sub_79E590: effect 12 through the dword_112A95C CHR, ++m_wE1E |  | V |
| 145 | 10 | 10 | m | dstSel:int, srcSel:int | int 0 | no | `FFX_AtelSys_Btl_145_CopyBattleName_resi` | copies the 40 byte m_battleName and m_bDD5 |  | V |
| 146 | 2 | 1 | s | sel:int | int | no | `FFX_AtelSys_Btl_146_GetHeightAboveGround_resi` | (ground height - y) * 100 |  | V |
| 147 | 36 | 32 | ms |  | int | no | `FFX_AtelSys_Btl_147_CanAct_resi` | sub_791780: +0x41C, +0x41D set, +0xDE7, +0x508, +0x432 clear and not the poison pending actor |  | V |
| 148 | 9 | 1 | s | from:float, to:float, frames:int | int 0 | no | `FFX_AtelSys_Btl_148_FadeAlphaMode_resi` | btl:54 with the mode from m_b423 |  | V |
| 149 | 2 | 1 | s |  | int | no | `FFX_AtelSys_Btl_149_GetMoveFramesLeft_resi` | sub_7A9320: frames left on the current move |  | V |
| 150 | 281 | 44 | mbs |  | int | no | `FFX_AtelSys_Btl_150_GetByteC9FA_resi` | byte_112C9FA, written by begin and FFX_Btl_FinishActions |  | V |
| 151 | 237 | 107 | mbs | event:int | int | yes | `FFX_AtelSys_Btl_151_FireHoldEventB_start`, `FFX_AtelSys_Btl_151_FireHoldEventB_poll` | btl:60 with the hold value 1 |  | V |
| 152 | 2 | 1 | s |  | int | no | `FFX_AtelSys_Btl_152_HasStatusBits616_resi` | sub_7AF2F0: the +0x616 status word has 0x2800 or 0x40 |  | V |
| 153 | 5976 | 633 | b | v:int | int v & 3 | no | `FFX_AtelSys_Btl_153_SetBtlByteC9CD_resi` | byte_112C9CD = v & 3, reset at begin, read by sub_780CE0 for the menu module 17 call |  | I |
| 154 | 0 | 0 |  |  | int | no | dead `FFX_AtelSys_Btl_154_IsNotPoisonPending_resi` | true when self is not g_ffxBtlPoisonPendingActor |  | V |
| 155 | 0 | 0 |  | sel:int | float | no | dead `FFX_AtelSys_Btl_155_GetUnitY_resf` | the unit position y |  | V |
| 156 | 27 | 8 | bs | selA:int, selB:int | float degrees | no | `FFX_AtelSys_Btl_156_GetPitchBetween_resf` | the elevation from one mask's point to the other's |  | V |
| 157 | 6 | 5 | ms |  | none | yes | `FFX_AtelSys_Btl_157_WaitSavedPosCleared_poll` | poll only, waits while self m_savedPosValid |  | V |
| 158 | 100 | 19 | ms | sel:int, bit:int, unused:int | int 0 | no | `FFX_AtelSys_Btl_158_FireMotionEvent_resi` | FFX_Btl_OnMotionEvent(unit, bit): sets the event bit and runs the hit, the effect or the reaction that bit stands for | rng 9*, rng 10-13*, local*, booster* | V |
| 159 | 12 | 3 | bs | sel:int | float degrees | no | `FFX_AtelSys_Btl_159_GetHomeFacingDelta_resf` | the home facing minus the current rotation |  | V |
| 160 | 193 | 4 | bs | on:int | int old | no | `FFX_AtelSys_Btl_160_SetByteC9FC_resi` | byte_112C9FC (byte 0 of the four bytes at 0x112C9FC, split 2026-10-29) = on & 1 |  | V |
| 161 | 625 | 161 | mbs | sel:int | int | no | `FFX_AtelSys_Btl_161_GetUnitOrderCode_resi` | an enemy's m_unitIndex, or -6 - its index in g_ffxBattlePartyOrder, 255 none |  | V |
| 162 | 193 | 118 | e | unused:int | int | yes | `FFX_AtelSys_Btl_162_LoadSystemMgrp_start`, `FFX_AtelSys_Btl_162_LoadSystemMgrp_poll` | start sub_79CCF0 starts the read of the system motion group through the asset loader, the poll waits for byte_112C9CF | load | V |
| 163 | 2 | 1 | s | motion:int | int | no | `FFX_AtelSys_Btl_163_PlayMotionForced_resi` | btl:7 with m_chrReady forced on for the call, then m_b3F3 = 1 when it played |  | V |
| 164 | 13 | 12 | e |  | int 0 | no | `FFX_AtelSys_Btl_164_ReleaseSystemMgrp_resi` | sub_79CCA0 frees the block. The clock hit is the allocator's one time heap cookie |  | V |
| 165 | 70 | 56 | mb | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Btl_165_SetBtlPoint_resi` | qword_112A8D0 xyz, w 1.0, byte 1 of dword_112C9FC = 1 |  | V |
| 166 | 7 | 7 | b | mode:int | int | no | `FFX_AtelSys_Btl_166_SetEscapeMode_resi` | g_ffxBtlEscapeMode 0x112C9FF: 0 roll, 1 never, 2 always (BATTLE_START_AND_END.md) |  | V |
| 167 | 0 | 0 |  | sel:int, index:int | float | no | dead `FFX_AtelSys_Btl_167_GetMoveParamOf_resf` | sub_7B2C90: attack start distance, offset, back jump, the run speeds, weight, attack height |  | V |
| 168 | 116 | 25 | m | sel:int, index:int, v:float | int 0 | no | `FFX_AtelSys_Btl_168_SetUnitFloatOf_resi` | FFX_BtlAi_SetUnitFloat on another unit |  | V |
| 169 | 836 | 76 | b | window:int | int | yes | `FFX_AtelSys_Btl_169_WaitMessageOrVoice_start`, `FFX_AtelSys_Btl_169_WaitMessageOrVoice_poll` | start clamps the window. The poll closes it on a confirm press, or by itself once the text phase is 3 and sub_8864B0 says no voice is busy | input, sound | V |
| 170 | 1300 | 76 | ms | prop:int | int | no | kept `FFX_AtelSys_Btl_170_GetSelfProperty_resi` | GetProperty(self, prop, -13) |  | V |
| 171 | 4536 | 298 | ms | prop:int, value:int | int | no | kept `FFX_AtelSys_Btl_171_SetSelfProperty_resi` | SetProperty(self, prop, -13, value) | rng 10-13*, local* | V |
| 172 | 472 | 59 | ms | index:int | float | no | `FFX_AtelSys_Btl_172_GetMoveParam_resf` | btl:167 on self |  | V |
| 173 | 1 | 1 | s |  | int | no | `FFX_AtelSys_Btl_173_AnyEquipFlag8_resi` | sub_7AD6E0: any equipment entry in use with flag 8 and without flag 2 |  | V |
| 174 | 4 | 3 | bs |  | int | no | `FFX_AtelSys_Btl_174_GetWordC9E2High_resi` | high byte of word_112C9E2 |  | V |
| 175 | 2 | 1 | s |  | int | no | `FFX_AtelSys_Btl_175_HasFieldGroundSection_resi` | sub_7986A0: field monster section byte 0 set and btl:5 clear |  | V |
| 176 | 150 | 140 | mbs | sel:int | int | no | `FFX_AtelSys_Btl_176_GetAttackFrames_resi` | the attack motion frame, scaled by the motion speed, plus the run up frames over the approach distance |  | V |
| 177 | 15 | 3 | mb | v:int | int 0 | no | `FFX_AtelSys_Btl_177_DebugPrintInt2_resi` | pops one value into a stubbed print |  | V |
| 178 | 2840 | 300 | ms | index:int, v:float | int 0 | no | `FFX_AtelSys_Btl_178_SetSelfFloat_resi` | FFX_BtlAi_SetUnitFloat(self, index, v) |  | V |
| 179 | 38 | 1 | s | v:int | int | no | `FFX_AtelSys_Btl_179_SetDwordC8F0_resi` | sub_7B2B60: dword_112C8F0 = v |  | V |
| 180 | 1 | 1 | s |  | int 0 | no | `FFX_AtelSys_Btl_180_HideSideChr_resi` | m_bF65 = 8, sub_79EA90, side record CHR alpha 0 |  | V |
| 181 | 36 | 32 | ms |  | int 0 | no | `FFX_AtelSys_Btl_181_ClearW400_resi` | self m_w400 = -1 |  | V |
| 182 | 1 | 1 | s | sel:int | int | no | `FFX_AtelSys_Btl_182_GetApproachTime_resi` | sub_7A9FC0(unit, 12) / the self run speed |  | V |
| 183 | 12 | 3 | b | window:int, msg:int | int 0 | no | `FFX_AtelSys_Btl_183_ShowSubtitleAlt_resi` | btl:114 with flags 0x6002 (sub_89B290) | local | V |
| 184 | 39 | 23 | b | v:int | int | no | `FFX_AtelSys_Btl_184_SetEscapeAllowed_resi` | g_ffxBtlEscapeAllowed |  | V |
| 185 | 0 | 0 |  | sel:int | float | no | dead `FFX_AtelSys_Btl_185_GetMaskPointX11_resf` | sub_795170(11, mask) x |  | V |
| 186 | 0 | 0 |  | sel:int | float | no | dead `FFX_AtelSys_Btl_186_GetMaskPointX10_resf` | sub_795170(10, mask) x |  | V |
| 187 | 1 | 1 | m | frames:int | int 0 | no | `FFX_AtelSys_Btl_187_StartHoldMove_resi` | self m_moveKind = 16 for frames |  | V |
| 188 | 6 | 3 | b | sel:int | none | yes | `FFX_AtelSys_Btl_188_WaitEffectAOf_start`, `FFX_AtelSys_Btl_188_WaitEffectAOf_poll` | btl:81 on another unit |  | V |
| 189 | 3 | 3 | b | sel:int | none | yes | `FFX_AtelSys_Btl_189_WaitEffectBOf_start`, `FFX_AtelSys_Btl_189_WaitEffectBOf_poll` | btl:99 on another unit |  | V |
| 190 | 84 | 26 | mbs | sel:int | float degrees | no | `FFX_AtelSys_Btl_190_GetUnitYaw_resf` | the unit rotation |  | V |
| 191 | 0 | 0 |  | v:float | int 0 | no | dead `FFX_AtelSys_Btl_191_SetAttachScale_resi` | self m_attachScale and FFX_Ch_SetAttachScale |  | V |
| 192 | 39 | 1 | s | sel:int | float | no | `FFX_AtelSys_Btl_192_GetUnitYOffset_resf` | maybe_FFX_Btl_GetUnitYOffset |  | V |
| 193 | 0 | 0 |  | selB:int, selA:int | int | no | dead `FFX_AtelSys_Btl_193_GetDistanceBetween_resi` | distance between the kind 1 points of two masks |  | V |
| 194 | 0 | 0 |  | selA:int, selB:int | float degrees | no | dead `FFX_AtelSys_Btl_194_GetYawBetween_resf` | the yaw from one mask's point to the other's |  | V |
| 195 | 87 | 26 | mbs | sel:int | float degrees | no | `FFX_AtelSys_Btl_195_GetHomeFacingOf_resf` | sub_7958C0, the home facing |  | V |
| 196 | 0 | 0 |  | sel:int | int | no | dead `FFX_AtelSys_Btl_196_HasEquipFlag8_resi` | either of the unit's two equipment entries has flag 8 |  | V |
| 197 | 1 | 1 | b |  | int | no | `FFX_AtelSys_Btl_197_IsGameOverSeq_resi` | byte 1 of g_ffxBtlGameOverSeq != 0 |  | V |
| 198 | 0 | 0 |  | index:int | int | no | dead `FFX_AtelSys_Btl_198_GetTableInt1136B18_resi` | dword_1136B18[index], 16 entries |  | V |
| 199 | 0 | 0 |  | index:int | float | no | dead `FFX_AtelSys_Btl_199_GetTableFloat1136AD8_resf` | flt_1136AD8[index], 16 entries |  | V |
| 200 | 0 | 0 |  | sel:int, id:int | int | no | dead `FFX_AtelSys_Btl_200_PlayStatusSeA_resi` | sub_79FDA0 |  | V |
| 201 | 110 | 34 | ms | sel:int, id:int | int | no | `FFX_AtelSys_Btl_201_PlayStatusSeB_resi` | sub_79FDF0 |  | V |
| 202 | 0 | 0 |  | sel:int, id:int | int | no | dead `FFX_AtelSys_Btl_202_PlayUnitSeLoop_resi` | sub_7A0140, the no dedupe form of btl:136 |  | V |
| 203 | 0 | 0 |  | sel:int, id:int | int | no | dead `FFX_AtelSys_Btl_203_PlayUnitSeOnceAwake_resi` | sub_7A0170: btl:136 unless m_statusCount[11] | global | V |
| 204 | 15 | 10 | m | sel:int | int mask | no | `FFX_AtelSys_Btl_204_SetTargetMask_resi` | self m_targetMask = mask |  | V |
| 205 | 23 | 6 | m | sel:int | int mask | no | `FFX_AtelSys_Btl_205_AddTargetMask_resi` | self m_targetMask OR= mask |  | V |
| 206 | 14 | 5 | m | sel:int | int mask | no | `FFX_AtelSys_Btl_206_RemoveTargetMask_resi` | self m_targetMask &= ~mask |  | V |
| 207 | 370 | 1 | s |  | int | no | `FFX_AtelSys_Btl_207_GetFieldSectionByte1_resi` | byte 1 of the battle field monster section |  | V |
| 208 | 1 | 1 | s | on:int | int 0 | no | `FFX_AtelSys_Btl_208_SetBtlByteC9DE_resi` | byte 3 of dword_112C9DB = on & 1 |  | V |
| 209 | 6 | 3 | m | sel:int | int | no | `FFX_AtelSys_Btl_209_GetEffectACount_resi` | m_bE12 of the unit |  | V |
| 210 | 0 | 0 |  | sel:int | int | no | dead `FFX_AtelSys_Btl_210_GetEffectBCount_resi` | m_bE13 of the unit |  | V |
| 211 | 6 | 3 | b | sel:int, id:int, param:int | int 0 | no | `FFX_AtelSys_Btl_211_PlayEffectAOf_resi` | btl:23 on another unit |  | V |
| 212 | 0 | 0 |  | sel:int, id:int, param:int | int | no | dead `FFX_AtelSys_Btl_212_PlayEffectBOf_resi` | btl:98 on another unit |  | V |
| 213 | 25 | 20 | m | sel:int | int unit | no | `FFX_AtelSys_Btl_213_SetSummoner_resi` | self m_summoner |  | V |
| 214 | 0 | 0 |  | attSel:int, defSel:int, ability:int | int | no | dead `FFX_AtelSys_Btl_214_PreviewDamage_resi` | FFX_Btl_DamageFormula with the random flag 0, so no draw |  | V |
| 215 | 4 | 4 | m | sel:int, kind:int | int 0 | no | `FFX_AtelSys_Btl_215_PlayLinkReaction_resi` | sub_78CA20, the link unit's reaction motion, unless the link unit is KO |  | V |
| 216 | 33 | 31 | b | on:int | int | no | `FFX_AtelSys_Btl_216_SetUnit13PartFlag_resi` | byte_112CA06 = on & 1 |  | V |
| 217 | 1 | 1 | s |  | int | no | `FFX_AtelSys_Btl_217_ApplyUnit13PartFlag_resi` | FFX_Ch_ModelSetHide(unit 13 CHR, 1, byte_112CA06) |  | V |
| 218 | 1 | 1 | s |  | int | no | `FFX_AtelSys_Btl_218_GetAdvantage_resi` | byte 1 of dword_112C9DB |  | V |
| 219 | 0 | 0 |  |  | int | no | dead `FFX_AtelSys_Btl_219_GetUnit13PartFlag_resi` | byte_112CA06 |  | V |
| 220 | 11 | 4 | m | sel:int, msg:int | int 0 | no | `FFX_AtelSys_Btl_220_SetBattleNameFromMsg_resi` | copies 40 bytes of message msg into the unit's m_battleName, half picked by the map name alt flag | local | V |
| 221 | 0 | 0 |  | v:int | int | no | dead `FFX_AtelSys_Btl_221_SetDwordC8FC_resi` | dword_112C8FC, read by sub_792F20 |  | V |
| 222 | 830 | 76 | b |  | int | no | `FFX_AtelSys_Btl_222_GetConfigBit9UnlessFfwd_resi` | FFX_Btl_GetOptionBit9UnlessFfwd: 0 while the booster battle fast forward is on, else bit 9 of the save battle config at 0x113079C. Every voiced line branches on it | booster | V |
| 223 | 17 | 1 | s | battleId:int | int | no | `FFX_AtelSys_Btl_223_IsBattleScene_resi` | a battle is running and its scene word equals battleId >> 16 |  | V |
| 224 | 94 | 86 | m |  | int | no | `FFX_AtelSys_Btl_224_CanCounter_resi` | sub_7B24A0(self, 0, 0, 1) and the issuer's m_bDF1 clear |  | V |
| 225 | 0 | 0 |  |  | int | no | dead `FFX_AtelSys_Btl_225_IssuerCounterAllowed_resi` | the issuer's m_bDF1 clear |  | V |
| 226 | 49 | 11 | mb | sel:int, v:int | int 0 | no | `FFX_AtelSys_Btl_226_SetTexAnim4_resi` | FFX_Ch_SetTexAnimPattern(chr, 4, v) |  | V |
| 227 | 6 | 6 | b | size:int | int 0 | no | `FFX_AtelSys_Btl_227_AllocScriptBlock_resi` | allocates size bytes into dword_112BE98, keeps the first size in dword_112BE9C |  | V |
| 228 | 0 | 0 |  | attSel:int, defSel:int | int | no | dead `FFX_AtelSys_Btl_228_GetHitReaction_resi` | sub_79A860: the defender's hit log reaction for the attacker's current command |  | V |
| 229 | 0 | 0 |  | sel:int, id:int, param:int | int 0 | no | dead `FFX_AtelSys_Btl_229_PlayFieldEffectA_resi` | sub_79ECA0 |  | V |
| 230 | 1 | 1 | s | sel:int, id:int, param:int | int 0 | no | `FFX_AtelSys_Btl_230_PlayFieldEffectB_resi` | sub_79EC40 |  | V |
| 231 | 7 | 1 | s | ability:int | int mask | no | `FFX_AtelSys_Btl_231_YojimboPickTargets_resi` | sub_7996D0: a group pick on stream 5 (sub_799910) or a FilterAndPickTarget on stream 4, by the ability flag, into self m_targetMask | rng 5, rng 4 | V |
| 232 | 1 | 1 | s |  | int | no | `FFX_AtelSys_Btl_232_GetGil_resi` | FFX_SaveData_GetGil |  | V |
| 233 | 2 | 1 | s |  | int | no | `FFX_AtelSys_Btl_233_GetYojimboWord_resi` | sub_7B2B30: dword_1130838 |  | I |
| 234 | 3 | 1 | e | v:int | int v | no | `FFX_AtelSys_Btl_234_SetYojimboWord_resi` | sub_7B2B40 |  | I |
| 235 | 2 | 1 | s |  | int | no | `FFX_AtelSys_Btl_235_RandStream17_resi` | FFX_Rand_Stream(17) | rng 17 | V |
| 236 | 0 | 0 |  | item:int | int | no | dead `FFX_AtelSys_Btl_236_GetItemCount_resi` | FFX_SaveData_GetItemCount |  | V |
| 237 | 8 | 1 | m | item:int, count:int | int | no | `FFX_AtelSys_Btl_237_AddItem_resi` | FFX_SaveData_AddItem |  | V |
| 238 | 2 | 1 | s | a:int, b:int | int ability | no | `FFX_AtelSys_Btl_238_PickZanmatoTier_resi` | a + b against 32 / 48 / 63 / 80 gives 12510..12514 |  | V |
| 239 | 1 | 1 | m | sel:int, v:int | int 0 | no | `FFX_AtelSys_Btl_239_SetChrFxParam_resi` | sub_79E4F0 -> sub_7FBDF0(chr, v) |  | I |
| 240 | 0 | 0 |  |  | float | no | dead `FFX_AtelSys_Btl_240_GetBattleFrameCounter_resf` | g_ffxBattleFrameCounter as a float |  | V |
| 241 | 3 | 1 | s |  | int 0 | no | `FFX_AtelSys_Btl_241_ClearAiList_resi` | fills the self 20 word list at dword_2310CD8 + 1144 * unit with 0xFF |  | V |
| 242 | 23 | 1 | s | ability:int | int 0 | no | `FFX_AtelSys_Btl_242_AppendAiList_resi` | writes into the first free word of that list |  | V |
| 243 | 14 | 1 | s | forcedId:int, threshold:int | int 0 or 1 | no | `FFX_AtelSys_Btl_243_RandPercent_resi` | FFX_Btl_RandPercentCheck: with forcedId and word_1136410 set it compares them and draws nothing, else stream 18 against threshold | rng 18 | V |
| 244 | 0 | 0 |  | ability:int, sel:int | int mask | no | dead `FFX_AtelSys_Btl_244_PreviewTargets_resi` | sub_7A8B40: self m_targetMask from sub_799790, 0 when the MP cost check fails |  | V |
| 245 | 0 | 0 |  | sel:int, ability:int | int | no | dead `FFX_AtelSys_Btl_245_GetMpCost_resi` | FFX_Btl_GetCommandMpCost (CHEAT_BATTLE.md) |  | V |
| 246 | 5 | 1 | s |  | int 0 | no | `FFX_AtelSys_Btl_246_ClearValueQueue_resi` | byte_11363EC = byte_11363ED = 0 |  | V |
| 247 | 21 | 1 | s | v:int | int v | no | `FFX_AtelSys_Btl_247_PushValueQueue_resi` | appends to the 16 entry word_11363F0 |  | V |
| 248 | 10 | 1 | s |  | int | no | `FFX_AtelSys_Btl_248_PopValueQueue_resi` | next entry, 0 when empty |  | V |
| 249 | 0 | 0 |  | a:int, sel:int, prop:int, value:int, flags:int | int unit | no | dead `FFX_AtelSys_Btl_249_PickWithFilter_resi` | sub_7A8F60: FilterAndPickTarget on stream 4, then sub_799A10 and the last pick mask | rng 4 | V |
| 250 | 19 | 1 | s | id:int | int 0 | no | `FFX_AtelSys_Btl_250_SetForcedRollId_resi` | word_1136410, which makes btl:243 and 251 compare instead of draw. Nothing else writes it, not even battle begin | global | V |
| 251 | 54 | 1 | s | ability:int, sel:int, prop:int, flags:int, threshold:int | int unit, 255 none | no | `FFX_AtelSys_Btl_251_MagusPickTarget_resi` | sub_7A8B40 preview, then RandPercentCheck on stream 18, then sub_7A8F60 on stream 4 | rng 18, rng 4 | V |
| 252 | 5 | 1 | s |  | int | no | `FFX_AtelSys_Btl_252_GetForcedRollId_resi` | word_1136410 |  | V |
| 253 | 67 | 1 | s | x:int, y:int | int | no | `FFX_AtelSys_Btl_253_SetCmdRange714_resi` | +0x726/+0x727 = y, +0x728/+0x729 = x, and +0x714 = 1 unless btl:250 is set |  | V |
| 254 | 3 | 1 | s | a:int, b:int, c:int, d:int | int 0 | no | `FFX_AtelSys_Btl_254_SetCmdRange714Ex_resi` | +0x714 = 1, +0x729 = a, +0x727 = b, +0x728 = c, +0x726 = d |  | V |
| 255 | 3 | 1 | s |  | int mask | no | `FFX_AtelSys_Btl_255_PublishTargetMask_resi` | dword_112C8D0 = self m_targetMask | global | V |
| 256 | 2 | 1 | s | sel:int | int | no | `FFX_AtelSys_Btl_256_AnyUnitBusy_resi` | sub_7A8BE0: any unit in the mask with m_b432 |  | V |
| 257 | 3 | 1 | s |  | int | no | `FFX_AtelSys_Btl_257_GetDbgFullSet_resi` | g_ffxBtlDbgFullSet | debug | V |
| 258 | 18 | 6 | b | sel:int, otherSel:int | int unit | no | `FFX_AtelSys_Btl_258_SetNeckTarget_resi` | neck look-at (was SetLinkTarget): `m_neckAimDelay` +0x439 = 1, `m_neckTarget` +0x438 = the other unit or -1 |  | V |
| 259 | 1 | 1 | b | sel:int, x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Btl_259_UnitLookAtPoint_resi` | sub_8350A0: CHR look at kind 3 on the point, look at on (the Ch library's ch:123) |  | V |
| 260 | 71 | 32 | mb | a:int, b:int, c:int | int | no | `FFX_AtelSys_Btl_260_PushTripleQueue_resi` | sub_79D2A0, an 8 entry queue at word_1136074 |  | I |
| 261 | 2 | 1 | s |  | none | yes | `FFX_AtelSys_Btl_261_WaitByteE17_poll` | poll only, waits for self m_bE17 != 0 |  | V |
| 262 | 9 | 3 | m | sel:int, flag:int | int | no | `FFX_AtelSys_Btl_262_TestCharEventFlag_resi` | FFX_SaveData_TestCharAbilityBit(unit, flag) |  | V |
| 263 | 36 | 32 | ms | scale:int | int | no | `FFX_AtelSys_Btl_263_GetFacingStep_resi` | (home facing - m_facing) / pi * scale |  | V |
| 264 | 36 | 32 | ms | rampFrames:int, holdFrames:int | int 0 | no | `FFX_AtelSys_Btl_264_StartTurnKind1AltIfFx_resi` | btl:6 with a3 = 1 |  | V |
| 265 | 7 | 7 | m | sel:int, noGround:int, base:float | int count | no | `FFX_AtelSys_Btl_265_SetAirborneMask_resi` | per unit m_b407 = 1, ground mode 0 when noGround, m_b505 = 1, m_b50C = 0, m_yOffsetBase = base |  | V |
| 266 | 1 | 1 | s |  | int old | no | `FFX_AtelSys_Btl_266_TestAndSetGate_resi` | byte_112CA10 (byte 1 of the four flag bytes at 0x112CA0F, split from one dword 2026-10-28), set to 1, returns the old value |  | V |
| 267 | 1 | 1 | s |  | none | yes | `FFX_AtelSys_Btl_267_WaitGate_poll` | poll only, waits for that byte != 0 |  | V |
| 268 | 1 | 1 | s |  | int | no | `FFX_AtelSys_Btl_268_DecCounterCA0D_resi` | high byte of dword_112CA0A, decremented while positive |  | V |
| 269 | 2 | 1 | s |  | int 0 | no | `FFX_AtelSys_Btl_269_ClearByte411_resi` | self m_b411 = 0 |  | V |
| 270 | 0 | 0 |  |  | int | no | dead `FFX_AtelSys_Btl_270_GetSlotByte0_resi` | first non zero sub_7B19D0 slot, byte 0 |  | V |
| 271 | 0 | 0 |  |  | int | no | dead `FFX_AtelSys_Btl_271_GetSlotByte1_resi` | same slot, byte 1 |  | V |
| 272 | 153 | 34 | b | sel:int | int unit | no | `FFX_AtelSys_Btl_272_EnableHeadSway_resi` | sub_834770(chr, 1): CHR +0x4D4, the random head sway of ch:118. With dword_11363BC set unit 5 becomes 0 | render, global | V |
| 273 | 153 | 34 | b | sel:int | int unit | no | `FFX_AtelSys_Btl_273_DisableHeadSway_resi` | sub_834770(chr, 0), the same hack | render, global | V |
| 274 | 1 | 1 | s | kind:int, f:float, rampFrames:int, holdFrames:int | int 0 | no | `FFX_AtelSys_Btl_274_StartTurnKindAlt_resi` | btl:87 with a3 = 1 |  | V |
| 275 | 144 | 144 | b |  | int 0 | no | `FFX_AtelSys_Btl_275_SetScriptMode1_resi` | g_ffxBtlScriptMode = 1 |  | V |
| 276 | 16 | 16 | b |  | int 0 | no | `FFX_AtelSys_Btl_276_SetScriptMode2_resi` | g_ffxBtlScriptMode = 2, which also suppresses the statistics and the steal and drop handlers (MONSTER_ARENA.md) |  | V |
| 277 | 4 | 2 | ms |  | int | no | `FFX_AtelSys_Btl_277_CancelTurn_resi` | sub_795E70: with a turn running, m_b42B = 12 |  | V |
| 278 | 1 | 1 | s |  | int 0 | no | `FFX_AtelSys_Btl_278_PlayStatusLoopFx_resi` | sub_79E510: the Sleep or Silence style loop by status-2 bits 0x40 and 0x2000 |  | V |
| 279 | 20 | 20 | m | sel:int, ability:int | int 1 | no | kept `FFX_AtelSys_Btl_279_AiAddCommandPrio64_resi` | AddCommandEntry with noReorder 1 and prio 64, after sub_7AA100 when m_b3F9 |  | V |
| 280 | 1 | 1 | m | v:int | int | no | `FFX_AtelSys_Btl_280_SetGuardByteCA11_resi` | byte_112CA11, the KO path guard SUMMONS.md left open |  | V |
| 281 | 11 | 11 | b | v:int | int | no | `FFX_AtelSys_Btl_281_SetCountersAllowed_resi` | `g_ffxBtlCountersAllowed 0x112CA12`, 1 at every battle start (`FFX_Btl_BeginBattle` writes 0x01000000 over the four bytes at 0x781312), read only by `FFX_Btl_CheckCounter` at 0x78C1FA, 0 refuses every counter. Renamed from SetByteCA12 2026-10-28, `tools/apply_btl_counter_names.py` |  | V |
| 282 | 1 | 1 | b | sel:int, x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Btl_282_SetChrVec804_resi` | sub_824C90: CHR +0x804..+0x80C, then sub_824CC0(chr, 1) |  | I |
| 283 | 0 | 0 |  | on:int | int | no | dead `FFX_AtelSys_Btl_283_SetByteCA13_resi` | byte 0 of dword_112CA13 |  | V |
| 284 | 1 | 1 | m | v:int | int | no | `FFX_AtelSys_Btl_284_SetWordC94E_resi` | word_112C94E, read by FFX_Btl_EndPhaseStep |  | V |
| 285 | 3 | 3 | m | v:float | int 0 | no | `FFX_AtelSys_Btl_285_SetBlobShadowParam_resi` | sub_830CB0: CHR +0x17C, the ch:133 field |  | V |
| 286 | 0 | 0 |  |  | int | no | dead `FFX_AtelSys_Btl_286_GetByteCA15_resi` | byte 2 of dword_112CA13 |  | V |
| 287 | 0 | 0 |  | window:int, msg:int, x:int, y:int, align:int | int 0 | no | dead `FFX_AtelSys_Btl_287_ShowMessage2_resi` | btl:66 on the second text table dword_112A998, gated by sub_784E40 and dword_1841D20 | local | V |
| 288 | 9 | 3 | bs | unused:int, msg:int | int 0 | no | `FFX_AtelSys_Btl_288_ShowSystemMessage_resi` | sub_79D090: message type 7 from the second table | local | V |
| 289 | 0 | 0 |  | window:int, msg:int | int 0 | no | dead `FFX_AtelSys_Btl_289_ShowSubtitle2_resi` | btl:183 on the second table | local | V |
| 290 | 0 | 0 |  | sel:int | int unit | no | dead `FFX_AtelSys_Btl_290_EnableHeadSwayPlain_resi` | btl:272 without the unit 5 hack | render | V |
| 291 | 2 | 1 | s | side:int | int | no | `FFX_AtelSys_Btl_291_AttachWeaponUnit5_resi` | sub_793330(5, side) |  | V |
| 292 | 38 | 1 | s | v:int | int v | no | `FFX_AtelSys_Btl_292_SetDwordC8F4_resi` | dword_112C250 = v, then sub_7B2B50 sets dword_112C8F4 when sub_784E40 |  | V |
| 293 | 5 | 1 | s |  | int 0 | no | `FFX_AtelSys_Btl_293_AttachWeaponUnit4_resi` | sub_793330(4, 0) when self is unit 4 |  | V |
| 294 | 23 | 18 | b | unit:int | int unit | no | `FFX_AtelSys_Btl_294_MarkUnitB3E_resi` | m_head.m_b3E = 1 and both side records +0x3E = 1 |  | V |
| 295 | 1 | 1 | b | slot:int, v:int | int | no | `FFX_AtelSys_Btl_295_SetMonsterShowVote_resi` | sub_7969A0: the show vote of monster unit slot + 20 |  | V |
