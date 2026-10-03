# Random encounters on a field map

How walking around a field map produces a random battle, what the check reads, and what a second
player would have to feed it.

All addresses are VAs. The project convention is RVA + 0x400000 = VA, so subtract 0x400000 for the
RVA that goes in `loader\workshop\include\ffx\addresses\`.

Everything below was read out of the IDB by decompiling and by checking every xref to each global.
Claims are marked **confirmed** when they came out of the disassembly or out of the shipped data
file, and **inferred** when they did not. There is one thing I could not settle and it has its own
section near the end, read that before you build anything on the formation picker.

Names and evidence comments for every function and global here are in the IDB.

## The headline

Five answers matter more than the rest.

1. **The check is one function, `FFX_Field_StepRandomEncounter 0x780D10`**, and it has exactly one
   caller, `FFX_Atel_StepFieldFrame 0x871BC0` at `0x871D4A`. It takes the per-frame walking distance
   as a plain float argument. It reads no position at all itself.
2. **What accumulates is DISTANCE WALKED in world units, not frames and not a timer.** Two single
   globals, `g_ffxEncDistRemain 0x112A9D8` and `g_ffxEncDistTotal 0x112A9DC`. One roll is spent per
   10.0 units of distance, and the chance of that roll rises linearly with total distance since the
   last battle.
3. **The distance comes from the ATEL context player position cache**, `ctx+536..544` minus
   `ctx+552..560` on context 0 only. That is the exact same one-slot cache `INTERACTION_PATH.md`
   section 3.2 is about, so the same "save, overwrite, call, restore" trick applies, and a second
   character's movement is invisible to it until you do that.
4. **The result is a deferred request flag, not a call.** `g_ffxBattlePendingKind 0x112A8E2 = 1` plus
   three parameter bytes. `FFX_Btl_MainStep 0x790C10` polls it at `0x790F70` and starts the battle.
   One flag and three bytes is the whole thing a co-op build has to replicate.
5. **Two of the starting premises were wrong.** The encounter roll uses RNG **stream 0**, not stream
   18, and stream 0 has exactly one call site in the whole binary. And `0x780D10` is not merely a
   reader of the booster, it *is* the encounter check.

## 1. The call chain, top to bottom

Confirmed, every edge walked by xref.

```
FFXApplication::animate 0x42F520
  FFX_MainStepLoop 0x822840
    FFX_MainStep 0x820AE0                       once per pending step
      FFX_Player__stepControl       at 0x821010   the player moves here
      FFX_Btl_MainStep 0x790C10     at 0x821015   <- CONSUMES the pending flag
      FFX_Atel_StepOnce 0x88D3D0    at 0x82101A
        FFX_Atel_StepFieldAll 0x872BD0            (was sub_872BD0)
          FFX_Atel_StepFieldContexts              contexts 0..6, actor scripts run here
          FFX_Atel_StepFieldFrame 0x871BC0
            FFX_Field_StepStrollRegen 0x782D60    HP Stroll / MP Stroll, same distance float
            FFX_Field_StepRandomEncounter 0x780D10  <- SETS the pending flag
          FFX_Field_StepMenuOpenRequest 0x873620
```

Note the order inside one sub-step: movement, then the battle poll, then the field step. So a flag
set by the encounter check in sub-step N is consumed at the top of sub-step N+1, which is the next
frame when the sub-step count is 1. Confirmed from the `FFX_MainStep` disassembly at
`0x821010`-`0x82101A`.

## 2. What accumulates, and by how much

Confirmed, read off `0x780E28`-`0x780E6E` and `0x780E85`-`0x780EF9`.

`FFX_Atel_StepFieldFrame` computes the distance first:

```
0x871C12  fld  [ecx+230h]   ; ctx+560  previous Z
0x871C1B  fsub [ecx+220h]   ; ctx+544  current  Z
0x871C2B  fld  [ecx+22Ch]   ; ctx+556  previous Y
0x871C31  fsub [ecx+21Ch]   ; ctx+540  current  Y
0x871C41  fld  [ecx+228h]   ; ctx+552  previous X
0x871C47  fsub [ecx+218h]   ; ctx+536  current  X
          call FFX_Vec3_LengthSq 0x88C3C0   ; dx*dx + dy*dy + dz*dz
          call FFX_Sqrtf        0x88C400   ; sqrt
0x871C6C  fstp [ebp+var_8]  ; var_8 = the real 3D distance moved this frame
```

Hex-Rays drops the sqrt result and reuses the squared value in its output. The disassembly does not:
`var_8` holds the sqrt and `var_8` is what both consumers receive. So the float really is a Euclidean
distance, not a squared one. Confirmed.

Inside `FFX_Field_StepRandomEncounter`:

```
g_ffxEncDistRemain += dist          // 0x112A9D8, a single global
g_ffxEncDistTotal  += dist          // 0x112A9DC, a single global
g_ffxEncZoneRateCache = rate        // 0x112A9D6, debug mirror only
steps = (int)(g_ffxEncDistTotal / 10.0)
if (g_ffxEncDistRemain <= 10.0) return 0
loop:
    if (steps > rate/2) {
        thr = ((steps - rate/2) << 8) / (4 * rate)
        g_ffxEncNoBattleProd *= (float)(256 - thr)     // 0x112A9E0, display only
        g_ffxEncProbScale    *= 256.0                  // 0x112A9E4, display only
        if ((u8)FFX_Rand_Stream(0) < thr) -> ENCOUNTER
    }
    g_ffxEncDistRemain -= 10.0
    if (g_ffxEncDistRemain <= 10.0) return 0
    goto loop
```

So, plainly: one roll per 10.0 world units walked, and the per-roll probability is
`64 * (steps - rate/2) / rate / 256`, i.e. zero until you have walked `5 * rate` units since the last
battle and then rising linearly. `rate` is the per-zone byte, 20 to 65 in the shipped data.

Two corroborations that the float is walking distance and not anything else.

- `FFX_Field_StepStrollRegen 0x782D60` is handed the same float and uses it to tick HP Stroll and MP
  Stroll every `g_ffxStrollDistStep 0x112A9EC` (3.0) units. That is an in-game mechanic whose units
  are known to be distance walked. Confirmed.
- The developer BattleInfo window `SG_DebugWin_BattleInfoProc 0x7DAED0` prints
  `"EN %3dp:%2d/%2d/%3d"` from `100 - (g_ffxEncNoBattleProd * 100 / g_ffxEncProbScale)`,
  `(int)(g_ffxEncDistTotal / 10)`, and `g_ffxEncZoneRateCache`. That is "accumulated percent chance",
  "steps", "rate". It prints `EN` in capitals when `g_ffxEncountersEnabled 0x112A9D7` is set and `en`
  in lower case when it is not. Confirmed from the format strings.

`g_ffxEncNoBattleProd` and `g_ffxEncProbScale` are a running product of `(1 - p_i)` and its
denominator. They exist only for that display. The roll does not read them. Confirmed, their only
non-write xrefs are in the debug window.

All four accumulators are reset by `FFX_Field_ResetEncounterAccum 0x780FF0`, which runs on a
successful roll, from `FFX_Btl_BeginBattle 0x781020`, and from `0x781C50` on map load.

## 3. Whose movement it reads

**It reads the single bound player's position cache on ATEL context 0, and nothing else.** Confirmed.

`FFX_Atel_StepFieldFrame` starts with `mov g_ffxAtelCtx, offset g_ffxAtelCtxArray` at `0x871BE0`, so
it always works on context 0, and then reads exactly six floats:

| offset | meaning |
|---|---|
| ctx+536 / +540 / +544 | player current x y z |
| ctx+552 / +556 / +560 | player previous x y z |

`g_ffxAtelCtxArray` is `0x1325BA0`, 568 bytes per context, reached through the pointer slot
`g_ffxAtelCtx 0x1326B28`. Those are the same six floats every ATEL trigger reads, documented in
`INTERACTION_PATH.md` sections 3.2 and 7.

The only writer that matters is `FFX_Atel_PullActorPosFromChr 0x869E40`, and its write is gated on
`actor+46 == *(u16*)(ctx+10)`, the bound player actor id. So the cache only ever tracks one
character. A remote player's CHR can move all it likes and these six floats will not notice.
Confirmed.

### The second thing it reads per player, which the starting question did not ask about

The zone index argument. `FFX_Atel_StepFieldFrame` passes `*(u8 *)(g_ffxSaveData + 16)` as the second
argument. That byte is **the 2-bit `enc` ground attribute of the walkmesh triangle under the bound
player's feet**, `FFX_Map_GroundAttrGetEnc 0x83D820`, which is `(CHR+0x828 >> 7) & 3`. It is copied
into `g_ffxSaveData+16` by `FFX_Atel_PullActorPosFromChr`, inside the same `actor+46 == ctx+10` gate.
Confirmed, and it matches `MAP_FORMAT.md`'s attribute table.

So there are **two** one-slot per-player inputs to the encounter check, not one:

| input | where | per-character source |
|---|---|---|
| distance moved this frame | ctx+536..544 vs ctx+552..560 | the CHR position, via `FFX_Ch_GetPos` |
| encounter zone 0..3 | `g_ffxSaveData+16` | that CHR's own `CHR+0x828` ground attribute |

Both are available per character from the character's own CHR. Neither reaches the encounter check
for anybody but the bound actor. Confirmed.

### And the one thing that is NOT per player

`g_ffxEncDistRemain` and `g_ffxEncDistTotal` are single globals with no per-character indexing
anywhere. Confirmed from their complete xref lists, which are 7 and 6 entries, all inside
`FFX_Field_StepRandomEncounter`, `FFX_Field_ResetEncounterAccum` and the debug window.

## 4. Where the encounter rate comes from

**It is per map and per walkmesh region, but it is NOT loaded per map.** Confirmed.

One resident file, read once:

```
FFX_Btl_Init 0x781700
  assetLoader[4](13)          select asset class 13 = the "battle" cd module
  assetLoader[5](0)           -> g_ffxEncTableSize 0x112A9D2
  FFX_MemAlloc               -> g_ffxEncTableBlob  0x112A9C4
  assetLoader[0](0, blob, 0, 0)   read it
```

Resolving the file name through the shipped PS2 cd index gives
**`ffx_ps2/ffx/master/jppc/battle/kernel/btl.bin`**, 4096 bytes. Confirmed, not inferred:
`cdrom.fid[13]` is 7798 and entry 7798 of `cdrom.fnd` is
`host0:/ffx/master/jppc/battle/kernel/btl.bin`. Both index files are in the VBF under
`ffx_ps2/ffx/proj/battle/jp/cddata/`, and `cdrom.def` in the same folder names module 13
`_cdindex_battle`. The `inpc` copy of `btl.bin` is byte-identical.

`FFX_Btl_InitEncounterTablePtrs 0x79D250` derives three globals from it. All confirmed by parsing the
real file, which matches exactly:

| global | VA | meaning |
|---|---|---|
| `g_ffxEncTableBlob` | `0x112A9C4` | the whole file. Header: +4 map table offset (0x10), +8 zone blob offset (0x550), +12 file size (0x1000) |
| `g_ffxEncMapTable` | `0x112A9C8` | 96 entries, 14 bytes each |
| `g_ffxEncZoneBlob` | `0x112A9CC` | the variable length zone records |
| `g_ffxEncMapCount` | `0x112A9D0` | 96, computed as (hdr[8] - hdr[4]) / 14 |

Map entry, 14 bytes, confirmed:

| off | field |
|---|---|
| +0 | `s16` map id. `FFX_Btl_FindEncounterSceneByMapId 0x79D1C0` linear searches this and returns -1 when the map is absent |
| +2 | `u16` offset into `g_ffxEncZoneBlob` |
| +4 | `u16` not used by this path |
| +6..+13 | 8 char battle scene base name, e.g. `bjyt02`, `mihn08`, `klyt00` |

Zone table at `g_ffxEncZoneBlob + entry[+2]`, confirmed:

| off | field |
|---|---|
| +0 | `u8` total formation count across all zones. Not used by this path |
| +1 | `u8` ZONE COUNT. `FFX_Field_StepRandomEncounter` bounds-checks the zone index against it |
| +2.. | the zone records, stride `2 * rec[0] + 5`, walked by `FFX_Btl_GetEncounterZoneRecord 0x79D210` |

Zone record, confirmed against all 192 records in the file:

| off | field |
|---|---|
| +0 | `u8` formation entry count |
| +1, +2 | `u8` `u8` the battle map word, little endian. Bit 0x400 set means a battle map |
| +3 | `u8` **ENCOUNTER RATE**. 0 means no random battles in this zone. Shipped values are 0, 20, 30, 40, 50, 60, 65 |
| +4 | `u8` the modulo divisor for the formation pick, equal to the sum of the weights |
| +5 + 2i | `u8` battle scene variant number, becomes the `_%02d` in the scene name |
| +6 + 2i | `u8` weight |

Worked example, Mi'ihen Highroad, confirmed by parsing the file. Map entry 20 is map id 218, scene
name `mihn08`, two zones. Zone 0 has rate 50, divisor 18, and 8 formations with variants 0..7 and
weights 3,3,3,3,2,1,2,1. Zone 1 has rate 40, divisor 3, and 4 formations with variants 20..23 and
weights 0,1,1,1. The zone records pack with no gaps and the next map's zone table starts exactly
where zone 1 ends, which is what makes the layout certain rather than plausible.

Is there a global holding the current map's rate or table pointer? Only partly.

- `g_ffxEncZoneRateCache 0x112A9D6` holds the rate byte of whatever zone the check last looked at. It
  is written every time the check gets past its gates and is read only by the debug window, so it is
  a usable read-only probe but it is not what the roll uses. Confirmed.
- `dword_112A9F0`, `dword_112A9F4`, `dword_112A9F8`, `dword_112A9FC` cache the map entry, zone table,
  zone record and formation entry pointers, but `FFX_Btl_BeginBattle` sets them at battle entry, not
  during the field step. Confirmed.
- During the field step the lookup is done fresh every frame from the scene id and the zone byte.
  There is no "current map rate" global. Confirmed.

The scene id used for the lookup is `FFX_SaveData_GetSceneId 0x88D690`, which is
`(u16)g_ffxSaveData[0]`, unless `g_ffxAtelSceneIdOverride 0xC5273C` is greater than 0, in which case
that wins. The override is -1 at `FFX_Atel_Init` and written by `0x870130`. Confirmed.

## 5. The "no encounters" gates

There are a lot of them. A mod that re-implements the check instead of calling it will bypass some.
The list is confirmed from the disassembly of `0x871BC0` and `0x780D10`.

### In `FFX_Atel_StepFieldFrame 0x871BC0`, all required before the check is even called

| gate | address | what it means |
|---|---|---|
| `g_ffxAtelCtxArray[0] & 1` | `0x871BD6` | context 0 is live. Set by `FFX_Atel_InstallContextCallbacks 0x8711A3` |
| `ctx+10 != 0xFFFF` | `0x871C77` | a player actor is bound |
| `ctx[0] & 2` | `0x871C7F` | the player-bound flag. Set by `FFX_Atel_BindPlayerChr 0x871AB0`, cleared by `FFX_Atel_UnbindPlayerChr 0x8719E0` |
| `ctx[1] & 4 == 0` | `0x871C87` | writer not found, see unsettled |
| `ctx[3] & 0x84 == 0` | `0x871C8C` | writer not found, see unsettled |
| `ctx[0] & 0x10 == 0` | `0x871C92` | a per-frame scratch bit, cleared by `FFX_Atel_StepFrame 0x8679F4` |
| `ctx[1] & 1 == 0` | `0x871C97` | cleared at the end of this same function, so a one-frame latch |
| `ctx[0] & 8` | `0x871C9C` | **the context's actors have finished booting**. Set by `FFX_Atel_BootAllActors 0x8683E0`, the callback the context ctor installs at ctx+80 |
| `ctx+500 > 6` | `0x871CA1` | the field has been running more than 6 ticks. With the previous row this is the just-loaded-a-map guard |
| `ctx+528 & 1` | `0x871CB3` | set to 1 by the context ctor `0x86EE10` and never cleared anywhere, so always true. Not a real gate |
| `g_ffxSaveData[19] & 6` | `0x871CB8` | see below, effectively always true |
| `ctx+528 & 4 == 0` | `0x871CBE` | **the main menu is not open**. `FFX_Field_StepMenuOpenRequest 0x873620` clears this bit at `0x87365F` and sets it at `0x873848` when it calls `FFX_MenuSys_RequestOpen` |
| `FFX_Btl_IsBattlePendingOrActive() == 0` | `0x871CF5` | `FFX_Btl_GetPhase() != 0 \|\| g_ffxBattlePendingKind != 0` |
| `ctx[3] & 3 == 0` | `0x871D04` | writer not found |
| `sub_886630() == 0` | `0x871D0A` | a thunk to `sub_886650` which is `return 0`. Dead gate in this build |
| `assetLoader[2](1) == 0` | `0x871D18` | no disc read in flight. No encounter while streaming |

Also, when `ctx+528 & 2` is set on entry it calls `FFX_Btl_CancelPendingBattle 0x781530` first, which
drops a queued battle. That bit is set by the three map-change paths `0x86FEC0`, `0x86FF40` and
`0x8729C0`. Confirmed.

About `g_ffxSaveData[19] & 6`. `FFX_Atel_StepFrame 0x8679E9` does
`saveData[19] = (saveData[19] & 0xFB) | 2` at the top of **every** context step, and nothing in the
binary ever sets bit 2, so by the time `FFX_Atel_StepFieldFrame` tests it the value is always
non-zero. The only other writer of bit 1 is `FFX_Atel_SetFieldStepAllowsEncounter 0x86F430`, whose
only caller is the ATEL script syscall `FFX_AtelSys_Core_226_resi 0x857DE2`, and
`FFX_Atel_GetFieldStepAllowsEncounter 0x86A9D0` reads it back for syscall Core 225. So a script can
clear it, but the next context step sets it again. This is confirmed as a gate that exists and is
confirmed as one that is satisfied every frame in practice. Treat it as vestigial, not as a
suppression mechanism.

### In `FFX_Field_StepRandomEncounter 0x780D10`

| gate | address | what it means |
|---|---|---|
| `g_boosterEncounterRate` | `0x780D73` | 0 zeroes the distance and bails, 1 falls through to `g_ffxEncountersEnabled`, 2 multiplies the distance by 10 before accumulating |
| `g_ffxBattlePendingKind == 2` | `0x780DAA` | a scripted battle is already queued and wins |
| scene index < 0 | `0x780DAF` | the map has no row in `btl.bin`. **This is the cleanest per-map "no random battles here"** |
| `dist == 0.0` | `0x780DC0` | the player did not move |
| `g_ffxEncountersEnabled == 0` | `0x780DD0` | **the No Encounters armour auto-ability**, see below |
| zone index out of range | `0x780DE4` | `zoneIndex < 0 \|\| zoneIndex >= zoneTable[1]` |
| `g_ffxDebugEncountersOn == 0` | `0x780DF8` | the developer menu switch, 1 in `.data` |
| `zoneRecord[3] == 0` | `0x780E11` | **rate 0 means this walkmesh region has no random battles**. 127 of the 192 shipped zone records are like this |
| `zoneRecord[4] == 0` | `0x780E1E` | no formations |

`g_ffxEncountersEnabled 0x112A9D7` is the No Encounters ability, confirmed and not inferred.
`FFX_SaveData_RecomputeEncountersEnabled 0x785B10` (was named `FFX_SaveData_AllPartyMembersOk`)
starts it at 1 and, for each of the 18 character records that `FFX_SaveData_IsCharInParty` accepts,
ANDs in `!(record[0x4E] & 2)`. `record+0x4E` is the third of three 16-bit equipment auto-ability
masks that `FFX_SaveData_RecomputeCharDerived 0x7860F0` ORs together out of the equipped armour's
ability rows, and that function ANDs the same bit into the global itself at `0x7868E0`. One party
member wearing the ability disables encounters for the whole party, which is the shipped behaviour.
It is recomputed by `FFX_Btl_Shutdown 0x781590` at the end of every battle and by
`FFX_SaveData_SetCharInParty`.

### Two gates that look live and are not

- `dword_112CA24` and `byte_112CA28` at the very top of `FFX_Field_StepRandomEncounter` are a forced
  encounter hook. `dword_112CA24` has **exactly one xref in the entire binary**, this read, so
  nothing ever writes it, and it lives in the zero-filled tail of `.data`. The branch is dead.
  Confirmed.
- `g_ffxEncUsesSceneReq 0x112A8F7` chooses between a scene transition request and the deferred flag
  at `0x780F84`. Its only writer, `FFX_Field_SetEncounterUsesSceneReq 0x780FE0`, is called once per
  ATEL frame from `FFX_Atel_StepFrame 0x8679DB` and always with 0. So the scene-request branch is
  dead and the deferred flag is always the path taken. Confirmed.
- `g_ffxBattleDisabled 0x112CA2C` gates `FFX_Btl_RequestScriptedBattle 0x781C90` only. The random
  encounter path never consults it. It is written by `AutoTestManager__execCommand`, a dev harness.
  Confirmed, so do not reach for it as a "no random battles" switch.

## 6. What actually starts the battle

**A deferred request flag.** This is the good case.

```
FFX_Field_StepRandomEncounter, at 0x780F9F onwards:
    g_ffxBattlePendingKind       0x112A8E2 = 1
    HIWORD(g_ffxBattleSceneAndMap 0x112C254) = sceneIndex
    g_ffxBattleZoneIndex         0x112C258 = zoneIndex
    g_ffxBattleFormationIndex    0x112C259 = formationIndex
    FFX_Field_ResetEncounterAccum(1)
    return -1
```

```
FFX_Btl_MainStep 0x790C10, at 0x790F70:
    if (g_ffxBattlePendingKind != 0)
        FFX_Btl_BeginBattle(0,
                            HIWORD(g_ffxBattleSceneAndMap),
                            g_ffxBattleZoneIndex,
                            g_ffxBattleFormationIndex)
```

Confirmed from the disassembly. `g_ffxBattlePendingKind` has three values:

| value | meaning | writer |
|---|---|---|
| 0 | nothing queued | `FFX_Btl_BeginBattle 0x78107E`, `FFX_Btl_CancelPendingBattle 0x781541`, `FFX_Btl_Init` |
| 1 | a random encounter | `FFX_Field_StepRandomEncounter 0x780F9F` (and the dead debug path at `0x780D4F`) |
| 2 | a scripted battle | `FFX_Btl_RequestScriptedBattle 0x781CEA`, reached from `FFX_AtelSys_Btl_002_start` |

`FFX_Btl_BeginBattle 0x781020` is the loader. It resolves the four table pointers, builds the battle
scene name into `byte_112C25A` as `<8 char base name>_%02d` from the map entry bytes +6..+13 plus the
chosen formation's variant byte, sets `g_ffxBtlPhase 0x112A8E0 = 5` and tail calls `sub_783020`.
Confirmed.

So the single point a co-op build has to replicate is four bytes: `g_ffxBattlePendingKind` plus the
scene index, zone index and formation index. A host that writes those four and lets the client's own
`FFX_Btl_MainStep` pick them up gets the whole battle hand-off for free, with no direct call, no
reentrancy worry, and no mid-frame state problem. That is as good as this question could have come
out.

Reentrancy note: `FFX_Field_StepRandomEncounter` refuses to overwrite a pending kind of 2, and
`FFX_Btl_IsBattlePendingOrActive` stops the whole field step while any battle is pending, so the flag
cannot be stacked. Confirmed.

## 7. The RNG, and the correction to the premise

The premise said stream 18 is the battle chance roll. **It is not.** Confirmed from the pushed
immediates at every static call site of `FFX_Rand_Stream 0x7988F0`:

| stream | site | what |
|---|---|---|
| 0 | `0x780EEA` | **the field random-encounter chance roll. This is the only stream 0 call site in the binary** |
| 1 | `0x780EFD` | the encounter's formation pick |
| 1 | `0x78D098` | `FFX_Btl_RollPreemptiveOrAmbush`, see below |
| 18 | `0x7A8B06` | `FFX_Btl_RandPercentCheck`, an in-battle percent check. The only stream 18 site |

Stream 0 having exactly one consumer in the whole program is the best possible news for lockstep: the
field encounter roll cannot be desynced by anything else drawing from the same stream. 17 call sites
push a register rather than an immediate, all of them inside the battle module `0x784000`-`0x7C6000`,
so a dynamic index could in principle reach stream 0 or 1 during a battle. That is a caveat, not a
known problem. Inferred.

A second correction while I was in there. `sub_78D090` carried a comment calling it an encounter roll
and saying `record+0x4A` bit 2 was "almost certainly the No Encounters auto-ability". Both are wrong,
and it is now `FFX_Btl_RollPreemptiveOrAmbush 0x78D090`. Its only caller is `sub_783020` at
`0x783213`, which `FFX_Btl_BeginBattle` tail calls, and it passes 0x20. The result goes into
`BYTE1(dword_112C9DB)` = `0x112C9DC`, the battle advantage byte, and the battle counter
`dword_11307A4` is bumped three instructions later. So it runs during battle setup, after the
encounter has already been decided, and it returns 0 normal, 1 pre-emptive, 2 ambush. The
`record+0x4A` bit 2 check subtracts `1 + threshold`, which forces the pre-emptive outcome and makes
the ambush outcome impossible, so that bit is First Strike or Initiative, not No Encounters. No
Encounters is `record+0x4E` bit 1 and goes through `g_ffxEncountersEnabled`. Confirmed.

## 8. The one thing I could not settle

The weighted formation pick does not agree with the shipped data, and I could not work out which side
of that I am misreading.

The loop, confirmed byte for byte (`0x780F23` is `C1 E8 04`, so the shift is real and not a Hex-Rays
artifact):

```
0x780F17  mov   ecx, [ebp+var_4]      ; ecx = the zone record
0x780F1A  add   ecx, 6
0x780F20  movzx eax, byte ptr [ecx]
0x780F23  shr   eax, 4                ; <- this
0x780F26  add   esi, eax
0x780F28  cmp   edx, esi              ; edx = FFX_Rand_Stream(1) % zoneRecord[4]
0x780F2A  jl    loc_780F84            ; encounter, formation index = ebx
0x780F2C  inc   ebx
0x780F2D  add   ecx, 2
0x780F30  cmp   ebx, edi              ; edi = zoneRecord[0]
0x780F32  jl    loc_780F20
```

The shipped `btl.bin` stores the weights raw in that byte, not in the high nibble. Across all 192
zone records in the file, `sum(zoneRecord[6 + 2i])` for `i` in `0 .. zoneRecord[0]-1` equals
`zoneRecord[4]`, the modulo divisor, in **192 of 192** cases. No byte in that position anywhere in
the file is 16 or greater. I brute forced every other candidate offset and both nibbles and nothing
else comes close. So with the `>> 4` the accumulator stays at 0, `remainder < 0` is never true, the
loop falls through to `0x780F34`, and a successful chance roll is silently discarded. That would mean
field random encounters never happen, which is obviously not what the shipped game does.

So one of my premises about this inner loop, or about which bytes are actually in memory at the time,
is wrong. What I am confident about: the chain, the gates, the accumulators, the distance source, the
zone index source, the rate byte, and the deferred flag. None of those depend on this loop. What this
unsettled item does affect is only *which* formation is chosen and whether the roll converts, so a
co-op build that treats the roll as host-authoritative and replicates the four result bytes is
unaffected either way.

The cheapest experiment that settles it: break at `0x780F84` (or set a hardware write watch on
`g_ffxBattlePendingKind 0x112A8E2`) and walk around Mi'ihen Highroad. If it trips, the loop does
succeed and my reading of the record is wrong somewhere. If `g_ffxEncDistTotal 0x112A9DC` climbs
while walking but the flag never goes to 1 from this site, the loop really is the problem and the
real encounter trigger is somewhere I have not found.

Other things I could not settle, all minor:

- Who writes `ctx[1]` bit 2, `ctx[3]` bit 0, bit 1, bit 2 and bit 7. I scanned every function that
  names `g_ffxAtelCtx` or `g_ffxAtelCtxArray` and every `or`/`and` against `[reg]` and `[reg+1]` in
  `.text` and found only the clears. They must be written through a pointer parameter or a wider
  store. They are gates on the encounter check, so a mod must not re-implement the gate list by hand.
- What `g_ffxSaveData[19]` bit 2 is for. `FFX_SaveData_GetFieldFlag19Bit2 0x86A9C0` reads it,
  `FFX_Atel_StepFrame` clears it every context step, and nothing sets it.
- The meaning of `zoneTable[0]` beyond "total formation count across the zones", which is what it
  equals in every record but which no code in this path reads.
- Whether `g_ffxEncDistRemain` keeping the threshold in `ebx` across a second loop iteration (so the
  `steps > rate/2` test on iteration 2 compares the threshold, not the step count) is intentional. It
  is what the code does, confirmed, but it only matters when more than 10 units of distance arrive in
  one frame.

## 9. What this means for co-op

### The verdict

**A per-player encounter pass is possible and it is cheaper than the trigger work, because the check
takes the distance as an argument rather than reading a position.** But the accumulator is one
bucket, so two moving players share one encounter meter, and that is a design decision rather than a
reversing problem.

Concretely:

1. `FFX_Field_StepRandomEncounter(sceneId, encZone, dist)` reads **no context and no CHR**. Everything
   per-player it needs is in its three arguments. So the `INTERACTION_PATH.md` section 7
   save-overwrite-call-restore dance is not even required for the encounter check. You can call it
   directly with any character's distance and any character's zone byte. Confirmed.
2. Both arguments are derivable per character with no engine state touched. The distance is the
   Euclidean distance between the character's CHR position this step and last step, which a mod can
   track itself from `FFX_Ch_GetPos`. The zone byte is `FFX_Map_GroundAttrGetEnc(chr)`, which reads
   `CHR+0x828` and is already per-CHR. Confirmed.
3. `g_ffxEncDistRemain` and `g_ffxEncDistTotal` are single globals, so **it is one bucket**.
   Confirmed. Two players walking fills it twice as fast, which means roughly double the encounter
   rate. If you want the vanilla rate you have to feed only one distance.
4. The engine already calls the check once per step for the bound player. Any extra call you add is
   on top of that, not instead of it, unless you suppress the engine's own call.

### The cheapest correct design

Host-authoritative, in three lines:

- Leave `FFX_Atel_StepFieldFrame`'s own call alone. It already accumulates for whichever character is
  bound on the host, with every gate honoured, every per-map rate correct, and the right RNG stream.
- On the host, for each *other* player that moved this step, add nothing. The one-bucket accumulator
  means a second call would double the rate, and the rate the player expects is the vanilla one.
  If you do want remote movement to count, the honest shape is: pick the single largest per-player
  distance this step and call the check once with that player's distance and that player's zone byte,
  rather than calling it once per player.
- Replicate the result, not the roll. The host sends the four bytes
  (`g_ffxBattlePendingKind = 1`, scene index, zone index, formation index) and each client writes them
  before its own `FFX_Btl_MainStep` runs. Under lockstep with a synced `g_ffxRandStreamState` the
  clients would in principle reach the same conclusion on their own, but replicating the four bytes
  is cheaper to verify and does not depend on stream 0 staying in step.

Reasons this is the right shape and not just the easy one:

- Calling the check once per player per step would also clobber `g_ffxEncZoneRateCache` and run the
  gate list once per player, and the gate list contains per-frame latches (`ctx[1]` bits 0 and 1 are
  cleared at the end of `FFX_Atel_StepFieldFrame`) that are not designed to be evaluated twice.
- The roll must happen on exactly one machine. `FFX_Btl_BeginBattle` is the point of no return and it
  is driven off the flag, so "host rolls, everyone obeys the flag" needs no engine patch at all.
- Suppressing the engine's own call is possible without a patch, by clearing `ctx+528` bit 2 or
  `g_ffxDebugEncountersOn 0xC421CC`, but both of those have other effects. If you need to take the
  check over completely, the clean lever is `g_boosterEncounterRate 0xC421D8 = 0`, which makes the
  engine's own call bail at `0x780D73` and costs nothing, and then drive your own call. Inferred, not
  tested in a running game.

### What a mod must not do

- Do not re-implement the gate list. Four of those bits have writers I could not find, two gates are
  dead in this build and two are vestigial, and the per-zone rate-0 case suppresses encounters on a
  large fraction of maps. Calling `FFX_Field_StepRandomEncounter` honours all of it for free, and
  calling `FFX_Atel_StepFieldFrame` honours even more.
- Do not force `g_ffxBattlePendingKind = 2` for a random encounter. The two values are distinguished
  downstream: `FFX_Btl_MainStep 0x790EE1` reads `word_112A9D4` to decide the return-to-field tasks,
  and `FFX_Field_ResetEncounterAccum(1)` zeroes it on the random path while
  `FFX_Btl_RequestScriptedBattle` fills it in.
- Do not call `FFX_Btl_BeginBattle 0x781020` directly to start a random battle. It frees and
  reallocates, calls `_exit(1)` on one failure path at `0x7810FF`, and expects to be reached from
  `FFX_Btl_MainStep` with the parameter globals already consistent. The flag is the supported door.

### Loader addresses this needs

Nothing has been added to `loader\workshop` for this yet. The RVAs a kit header would want:

```
FFX_Field_StepRandomEncounter   0x00380D10   int (int sceneId, int encZone, float dist)
FFX_Field_ResetEncounterAccum   0x00380FF0   void (int alsoClearReturnTasks)
FFX_Btl_IsBattlePendingOrActive 0x00380C90   int ()
FFX_Btl_FindEncounterSceneByMapId 0x0039D1C0 int (int mapId)
FFX_Btl_GetEncounterZoneTable   0x0039D170   u8 * (int sceneIndex)
FFX_Btl_GetEncounterZoneRecord  0x0039D210   u8 * (int sceneIndex, int zoneIndex)
FFX_SaveData_GetSceneId         0x0048D690   int ()
FFX_Map_GroundAttrGetEnc        0x0043D820   int (CHR *)
g_ffxBattlePendingKind          0x00D2A8E2   u8
g_ffxBattleSceneAndMap          0x00D2C254   u32, scene index in the high word
g_ffxBattleZoneIndex            0x00D2C258   u8
g_ffxBattleFormationIndex       0x00D2C259   u8
g_ffxEncDistRemain              0x00D2A9D8   float
g_ffxEncDistTotal               0x00D2A9DC   float
g_ffxEncZoneRateCache           0x00D2A9D6   u8, read-only probe
g_ffxEncountersEnabled          0x00D2A9D7   u8, the No Encounters aggregate
g_ffxEncMapTable                0x00D2A9C8   u8 *, 14 bytes per entry
g_ffxEncZoneBlob                0x00D2A9CC   u8 *
g_ffxEncMapCount                0x00D2A9D0   s16
g_ffxDebugEncountersOn          0x008421CC   int, 1 by default
g_boosterEncounterRate          0x008421D8   int, 0 off / 1 normal / 2 high
```

`g_ffxEncDistTotal` is the single best runtime probe in this whole area. Log it once a second while
walking and it should climb at roughly the character's speed in world units per second, which
confirms the position cache, the gate list and the accumulator in one reading with no breakpoints.
