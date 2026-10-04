# Battle cheats: god mode, instant kill, overdrives

God mode (no damage, free MP), instant kill, overdrive gauge editing, overdrive mode unlocks and
per-character overdrive abilities. Written for the ImGui cheat plugin, so every list in here comes
with a table address, a stride and a count, because the UI has to be built from game data rather
than from typed-in names.

**Addresses in prose and in the tables are RVAs**, that is the IDA VA minus 0x400000. The project
headers store RVAs.

Target: `FINAL FANTASY X HD Remaster`, `FFX.exe`, the Steam build in the project IDB.

## Headline

The retail executable still contains the development team's battle debug flag block, 48 bytes at
RVA 0xD2A8F8 to 0xD2A927, and it already implements most of what we want. Three of those bytes are
the whole of god mode and free casting, and a fourth is a working instant kill. **No detour is
needed for the basic feature set.** A detour is only needed if the cheat has to be side selective
in a way the shipped flags are not.

| Want | Shipped flag | RVA | Effect |
|---|---|---|---|
| Party takes no HP or MP damage | `g_ffxBtlDbgPlyInvincible` | 0xD2A8F9 | the HP and MP subtractions are skipped for units 0..19 and 28..30 |
| Casting costs no MP | `g_ffxBtlDbgMagFree` | 0xD2A901 | the MP cost resolves to 0, which also passes the affordability gate |
| Overdrive usable at any gauge | `g_ffxBtlDbgLimitBreakOn` | 0xD2A90C | the gauge-is-full requirement is bypassed |
| Every hit does the maximum number | `g_ffxBtlDbgDmgIs100000` | 0xD2A910 | damage is forced to 100000, then clamped to 9999 or 99999 |
| Every enemy starts the battle with 1 HP | `g_ffxBtlDbgMonHp1` | 0xD2A91E | one hit kills anything, including bosses over the damage cap |
| Set every enemy's HP to 1 right now | `FFX_Debug_SetAllMonsterHp(1)` | 0x384BC0 | a shipped function, no argument marshalling |

All of those bytes are plain `.data`, zero at startup, and nothing in the retail binary writes them
except the debug window and two ATEL script syscalls. They are not gated on `g_ffxDebugMode`.

The two things the flags do **not** give us are a side-selective maximum damage (the damage override
applies to incoming attacks too) and status immunity (invincible stops the HP write, not Sleep or
Death). Both are covered by a single detour on `FFX_Btl_ComputeEffectForTarget 0x38E630`, which is
the recommendation in section 8.

## 1. The damage path, end to end

The chain is two phased: the numbers are computed and parked in the TARGET's own record, and a
later step applies them. That is why there are two obvious hook points rather than one.

```
FFX_Btl_ApplyActionToTargets   0x3892E0   picks the targets
  FFX_Btl_StageEffectsForTarget 0x389740  per target: snapshot, then loop the entries
    FFX_Btl_ComputeEffectForTarget 0x38E630   THE FORMULA AND THE CAP
      FFX_Btl_DamageFormula      0x389BF0 the raw number, stat by stat
      ... hit/crit/element/status modifiers, 20-odd small functions
      the clamp loop at 0x38ED78             writes entry+32 / +36 / +40
  (time passes, animations play)
FFX_Btl_EffectCompleteStep     0x38D980   the effect-completion path
  FFX_Btl_ApplyPendingEffects  0x38F060   per target: walk the parked entries
    FFX_Btl_ApplyHpDamage      0x38E230   actor+0x5D0 -= amount
    FFX_Btl_ApplyMpDamage      0x38E3B0   actor+0x5D4 -= amount
    FFX_Btl_ApplyCtbDelay      0x38E1E0   actor+0x65C += amount
    status words and counters copied over actor+0x606 / +0x616 / +0x608..0x614
  FFX_Btl_OnUnitDefeated       0x38C740   HP <= 0, or the death bit, becomes a KO
```

Verified by decompiling every function in the chain, not inferred from naming.

### 1.1 Where the numbers are parked

The target actor carries its own pending-effect block, which is what makes a replayed or injected
command work without a side channel.

```
tgtActor+0x774   pending effect block 1: 24 byte header, then 16 entries of 44 bytes at +0x78C
tgtActor+0xA4C   pending effect block 2: identical, 728 bytes later
```

`FFX_Btl_ApplyPendingEffects` loops exactly twice with a stride of 728, and (728 - 24) / 44 is 16
exactly, which is where the entry count comes from.

Header fields used by the applier, relative to the block base:

| off | meaning |
|---|---|
| +0x00 | byte, entries already consumed |
| +0x01 | byte, entry count |
| +0x02 | byte, the attacking unit index, 0xFF when the block is free |
| +0x03 | byte, the attacker's action id, matched alongside +0x02 |
| +0x05 | byte, flags: 1 = applied, 2 = AI notified, 4 = animation queued |

The 44-byte entry, as written by `FFX_Btl_ComputeEffectForTarget` and read by
`FFX_Btl_ApplyPendingEffects`:

| off | size | meaning |
|---|---|---|
| +0x00 | byte | effect kind, drives the reaction animation |
| +0x01 | byte | hit class, passed through to the floating number |
| +0x02 | byte | element / attack kind, passed through |
| +0x03 | byte | hit-or-miss class |
| +0x04 | byte | extra flag |
| +0x06 | byte | OR'd with ability row +90 |
| +0x07 | 13 bytes | status turn counters, copied to actor+0x608..0x614 |
| +0x14 | word | status word 1, copied over actor+0x606 |
| +0x16 | word | status word 2, copied over actor+0x616 |
| +0x18 | word | resource mask: 1 = HP slot valid, 2 = MP slot, 4 = CTB slot, 0x80 = overkill |
| +0x1A | word | status-applied mask |
| +0x1C | dword | a variance-free reference damage, for the display |
| **+0x20** | **dword** | **HP delta. Positive is damage, negative is healing.** |
| **+0x24** | **dword** | **MP delta** |
| **+0x28** | **dword** | **CTB delay delta** |

### 1.2 The battle unit struct offsets

All relative to `FFX_Battle_GetActor(index)` (RVA 0x394020). Confidence notes are per row because a
wrong offset here corrupts something else.

| off | size | field | how it was proved |
|---|---|---|---|
| **+0x594** | dword | **max HP** | `FFX_Btl_LoadCharRecordIntoActor` writes record+0x24 here, and `FFX_Btl_ApplyHpDamage` uses it as the clamp ceiling. Certain. |
| **+0x598** | dword | **max MP** | same function writes record+0x28 here, `FFX_Btl_ApplyMpDamage` clamps to it. Certain. |
| +0x59C | dword | max HP, second copy | `FFX_Btl_LoadCharRecordIntoActor` writes record+0x24 again |
| +0x5A0 | dword | max MP, second copy | same, record+0x28 |
| +0x5A4 | dword | max HP used for the OVERKILL test | `FFX_Btl_LoadUnitParams` writes record+0x24 to actor+0x540+0x64, and `FFX_Btl_ComputeEffectForTarget` tests `that - hpDamage <= 0` to set entry bit 0x80 |
| **+0x5BB** | byte | **selected overdrive mode, 0..19** | `FFX_Btl_LoadUnitParams` 0x39B599 copies record+0x38 here, and six functions dispatch on it against the constants 0..0x13. Certain. |
| **+0x5BC** | byte | **overdrive gauge** | record+0x39 at 0x39B5A0, and it is the value `FFX_Btl_AddOverdrive` raises. Certain. |
| **+0x5BD** | byte | **overdrive gauge max** | record+0x3A at 0x39B5A7, and it is the clamp ceiling in `FFX_Btl_AddOverdrive`. Certain. |
| +0x5BE | byte | gauge save slot for the temporary force-full | `0x3B06E0` / `0x3B06B0` pair |
| +0x5BF | byte | 1 while that temporary override is active | same |
| +0x5CC | dword | total AP | record+0x14 at 0x39B5DC |
| **+0x5D0** | dword | **current HP** | record+0x1C at 0x39B5E5, and the dword `FFX_Btl_ApplyHpDamage` subtracts from. Certain. |
| **+0x5D4** | dword | **current MP** | record+0x20 at 0x39B5EE, and the dword `FFX_Btl_ApplyMpDamage` and `FFX_Btl_SpendCommandCost` subtract from. Certain. |
| **+0x606** | word | **status word 1** | written from entry+0x14. Bit 0x0001 = KO, 0x0004 = removed from the field, 0x0100 / 0x0200 / 0x0400 / 0x0800 gate acting. `FFX_Btl_LoadUnitParams` 0x39BB32 sets bit 0 and actor+0xDCC = 2 for a unit loaded dead. |
| **+0x616** | word | **status word 2** | written from entry+0x16. Bit 0x0100 is the non-HP DEATH cause, see 2.3. 0x0400 blocks overdrive gain, 0x4000 = confuse. |
| +0x640 | byte | derived status flags | bit 4 zeroes MP costs, bit 8 = Zombie (healing becomes 9999 damage), bit 0x20 / 0x40 scale overdrive gain |
| +0x65C | byte | CTB counter | `FFX_Btl_ApplyCtbDelay` adds to it, clamped 0..255 |
| **+0x6BC** | word | **auto-ability flags 1** | OR'd from equipment ability row +98. Bit 0x4000 = Half MP Cost, 0x8000 = One MP Cost |
| **+0x6BE** | word | **auto-ability flags 2** | OR'd from ability row +100. **Bit 0x800 = Break Damage Limit.** Bits 1 / 2 double / triple overdrive gain, bit 4 doubles at low HP, bit 8 = Overdrive to AP |
| +0x6C0 | word | auto-ability flags 3 | OR'd from ability row +102 |
| +0x6CC | byte | cached MP cost | `FFX_Btl_CheckCommandCost` writes it, `FFX_Btl_SpendCommandCost` spends it |
| +0x6CD | byte | cached overdrive cost | same pair |
| **+0x6E4** | dword | working HP during an action | `FFX_Btl_StageEffectsForTarget` seeds it from +0x5D0 |
| +0x6E8 | dword | working MP | seeded from +0x5D4 |
| +0x6EC | dword | working CTB | seeded from +0x65C |
| +0x6F0 | dword | per-battle "this overdrive mode already counted" bitmask | `FFX_Btl_BumpOverdriveModeCounter` |
| +0x774 | 728 | pending effect block 1 | section 1.1 |
| +0xA4C | 728 | pending effect block 2 | section 1.1 |
| +0xD34 | dword | HP damage accumulated this action | the clamp loop adds to +0x6E4+0x650 |
| +0xD38 | dword | MP damage accumulated | same loop, next slot |
| +0xD3C | dword | CTB accumulated | same |
| +0xDCC | byte | KO flag, non-zero means already dead | `FFX_Btl_OnUnitDefeated` early-outs on it |
| +0xDEC | byte | predicted "this will die" | `FFX_Btl_StageEffectsForTarget` sets it |
| +0xF60 | dword | overkill accumulator, seeded from max HP | `FFX_Btl_ApplyPendingEffects` subtracts each hit, `FFX_Btl_OnUnitDefeated` tests `<= 0` |

The pre-existing offsets in `reversing/BATTLE_COMMAND.md` section 3 are consistent with all of this
except one: that table lists "+0x5D4 current MP, a dword" and does not list current HP at all.
Current HP is +0x5D0. Both are confirmed from `FFX_Btl_LoadUnitParams` writing record+0x1C and
record+0x20 into adjacent dwords.

### 1.3 Player unit or enemy unit

`FFX_Btl_IsEnemySlot 0x39AEF0` is literally `(unsigned)(index - 20) <= 7`, so enemies are indices
20..27 and everything else is a player-side unit. That is the test the engine itself uses on the
damage path, inside `FFX_Btl_IsUnitDamageable`.

Do not use `FFX_Btl_IsAllyUnit 0x393650` for this: it is `index <= 30`, which is true for enemies
too. The two names are easy to mix up and only one of them is the side test.

## 2. The damage cap

### 2.1 The constants and the clamp site

Inside `FFX_Btl_ComputeEffectForTarget 0x38E630`, read off the instruction stream:

```
0x38ECCA   mov  eax, 800h
0x38ECCF   and  ax, [ebx+6BEh]        ; ebx = the ATTACKER's actor
0x38ECD9   movzx eax, word ptr [esi+20h]   ; esi = the ability row, word at +0x20
0x38ECDD   neg  ebx
0x38ECDF   sbb  ebx, ebx
0x38ECE1   and  ebx, 15F90h           ; 90000
0x38ECE7   add  ebx, 270Fh            ; 9999  ->  cap = 9999 or 99999
0x38ECED   test al, al
0x38ECEF   jns  short 0x38ECF8
0x38ECF1   mov  ebx, 1869Fh           ; ability row +0x20 bit 0x80 forces 99999
0x38ECF8   test al, 40h
0x38ECFC   mov  ebx, 270Fh            ; ability row +0x20 bit 0x40 forces 9999
...
0x38ED78   the clamp loop, three slots, clamping each to [-cap, +cap]
```

So:

* **The cap is 9999.** It becomes **99999** when the attacker's auto-ability word at actor+0x6BE has
  bit 0x800 set, which is Break Damage Limit, folded in from equipment by
  `FFX_Btl_LoadCharRecordIntoActor 0x39C5F0` (ability row word +100 OR'd into actor+0x6BE).
* The ability row's own flag word at +0x20 overrides the per-attacker answer: bit 0x80 forces 99999
  regardless of equipment, bit 0x40 forces 9999 regardless.
* The clamp is symmetric, so **healing is capped at 9999 / 99999 too**, with the same cap value.
* It applies to all three slots, so MP damage carries the same cap. Current MP has a second, tighter
  cap of 9999, applied in `FFX_Btl_SpendCommandCost 0x38E5A0`.

### 2.2 Should a plugin write above the cap or disable the clamp

**Write above it and let it clamp.** That is exactly what the shipped debug flag does: setting
`g_ffxBtlDbgDmgIs100000 0xD2A910` makes the code store 100000 into the HP and MP values just before
the clamp, and the clamp reduces it to 9999 or 99999 depending on the attacker's gear. Nothing
downstream misbehaves.

Do not disable the clamp. The floating number renderer is digit based and the HUD, the overkill test
and the `g_ffxBtlDealt99999Flag` tracker all assume five digits. If the cap genuinely has to move,
the two immediates are at RVA 0x38ECE9 (the 9999 base, inside `add ebx, 270Fh`) and RVA 0x38ECE3
(the 90000 bonus, inside `and ebx, 15F90h`), but I would not ship that.

### 2.3 Instant kill: 99999 is not enough, and there is a better lever

99999 is the ceiling and several fiends have far more HP than that, so a maximum-damage hit is not a
one-hit kill on them. Three options, worst to best.

**Worst: just do 99999.** Fine for ordinary encounters, useless against the superbosses that are the
reason someone turns an instant-kill cheat on.

**There is a non-HP death bit, and it is not the clean answer.** `FFX_Btl_OnUnitDefeated 0x38C740`
opens with

```c
if ( (victim[0x617] & 1) != 0 )        // = actor+0x616 word, bit 0x0100
    cause = 2;                          // death by status
else if ( victim->curHp <= 0 )
    cause = 1;                          // death by damage
else
    return 0;
```

So setting bit 0x0100 of the status word at actor+0x616 and then calling
`FFX_Btl_OnUnitDefeated(killerIndex, victimIndex, 0, 1)` does kill the unit at any HP. But the bit
is a real game status with its own reaction animation (`0x3891B0` dispatches on the transition) and
it also blocks the unit from being targeted or acting (`0x3B24A0`), so arming it and then not
finishing the kill leaves the fight in a strange state. I verified the test site and the
animation dispatch. I did **not** identify which named FFX status it is, so I am not going to claim
it is "Death" or "Petrify".

**Best, and it is already in the binary: make the enemy have 1 HP.**

* `g_ffxBtlDbgMonHp1 0xD2A91E`, a byte. Non-zero makes `FFX_Btl_LoadUnitParams 0x39B4F0` store 1
  into the unit's current HP dword at load time (site 0x39BAF6, `mov dword [ebx+90h], 1` with
  `ebx = actor+0x540`). Set it once and every enemy in every subsequent battle enters with 1 HP, so
  literally any hit kills it, at any HP total, with no cap problem and no status weirdness. The
  player twin is `g_ffxBtlDbgPlyHp1 0xD2A91D` and shares the same store, so leave that one alone.
* `FFX_Debug_SetAllMonsterHp 0x384BC0` is `void (int toOne)`. With a non-zero argument it sets the
  current HP of units 20..27 to 1 and refreshes their HUD, immediately, mid battle. With 0 it
  restores them to max. It checks `FFX_Battle_IsActive()` itself, so it is safe to call from a
  per-frame hook. This is the right thing to bind to an "instant kill now" button.
* `FFX_Debug_SetAllPartyHp 0x384C80` is the same for units 0..17, and with argument 0 it is a free
  full party heal.
* `g_ffxBtlDbgKillAllHp 0xD3338C`, a dword read in `FFX_Btl_ApplyHpDamage`. Non-zero makes every HP
  damage event first do `curHp -= 99999`, on every unit including the party, with an exemption for
  units whose actor+4 is 1..7. Listed for completeness. It is blunt and not side selective, so
  prefer MonHp1.

Recommendation: **1 HP, not a big number.** See section 8.

## 3. God mode, the damage half

`FFX_Btl_IsUnitDamageable 0x38D3A0` is the engine's own switch and it is one line:

```c
return !(IsEnemySlot(i) && g_ffxBtlDbgMonInvincible)
    && !(!IsEnemySlot(i) && g_ffxBtlDbgPlyInvincible);
```

It has exactly three callers: `FFX_Btl_StageEffectsForTarget` (for the predicted-death flag),
`FFX_Btl_ApplyHpDamage` and `FFX_Btl_ApplyMpDamage`. In the two appliers it gates the subtraction
itself, so with `g_ffxBtlDbgPlyInvincible 0xD2A8F9` set to 1 the party's HP and MP dwords are never
written by the damage path.

**So the recommendation for the damage half of god mode is one byte, not a detour.** Prefer the thing
the game already does.

Three honest caveats, all verified:

1. **The floating damage number still appears.** `FFX_Btl_ShowFloatingNumber 0x39FA00` is called
   before the gate, so the player sees "2500" and loses nothing. Cosmetically odd, functionally
   fine. This is the shipped behaviour, it is not a bug we introduced.
2. **Statuses still land.** The gate covers the HP and MP writes only. The status words at actor+0x606
   and +0x616 and the 13 counters are copied over unconditionally in `FFX_Btl_ApplyPendingEffects`,
   so Sleep, Silence, Confuse and the death bit all still apply. `g_ffxBtlDbgDmgStatusOff 0xD2A906`
   blocks status application, but globally, including the player's own debuffs on enemies, so it is
   not usable as part of a party-only god mode.
3. **CTB delay still lands.** `FFX_Btl_ApplyCtbDelay 0x38E1E0` is not behind the gate.

Two alternatives, for completeness:

* **Per-unit fields.** Writing actor+0x5D0 back to actor+0x594 every frame is a HP regenerator, not
  invincibility, and it loses the fight against a single hit bigger than max HP. Not recommended.
* **The Invincible booster.** `FFX_Booster_InvincibleRefillUnit 0x3847C0` is what the shipped
  Invincible booster does: on that unit's turn it restores HP and MP to max, fills the overdrive
  gauge, and emits the heal as a real battle event. Driven by `g_boosterInvincible` at RVA 0xEFB7CC.
  It is a between-turns top-up, so the unit can still be killed within one turn. Mentioned because
  it is the closest shipped thing to god mode and because it is the cleanest existing proof that a
  full gauge write is legitimate.

If statuses have to be blocked for the party only, that needs the detour in section 8, and the same
detour covers maximum damage. One hook, both features.

## 4. God mode, the MP half

`FFX_Btl_GetCommandMpCost 0x38C690` is `int __cdecl (unsigned char actorIndex, int abilityRow,
int extraCost)`. It returns the MP cost, or -1 meaning "this actor may not use this ability at all".
The body, decompiled:

1. ability row +38 non-zero (an overdrive-gated ability) and the gauge actor+0x5BC is below max
   actor+0x5BD, or actor+0x616 has bit 0x400 -> return -1, **unless**
   `g_ffxBtlDbgLimitBreakOn 0xD2A90C`.
2. ability row +28 has 0x20000 and actor+0x609 is set -> return -1.
3. `cost = FFX_Btl_GetRawMpCost(actorIndex, abilityRow)` (0x38CF70: row+37 base, halved by
   actor+0x6BC bit 0x4000, forced to 1 by bit 0x8000, doubled for Doublecast, zeroed by actor+0x640
   bit 4).
4. **`if (FFX_Btl_IsEnemySlot(actorIndex) || g_ffxBtlDbgMagFree) { cost = 0; extraCost = 0; }`**
5. current MP, the dword at actor+0x5D4, below `cost + extraCost` -> return -1.
6. otherwise return cost.

Note step 4 carefully. Enemies **already** pay nothing, so `g_ffxBtlDbgMagFree 0xD2A901` changes the
player side only. That is exactly the semantics a cheat wants and it is a happy accident rather than
something I had to arrange.

### 4.1 Is returning 0 sufficient

Yes. Every caller was checked, there are five:

| caller | RVA | what it does with the result |
|---|---|---|
| `FFX_Btl_CheckCommandCost` | 0x38AB20, two sites | the execution gate. Caches the cost in actor+0x6CC and the overdrive cost in actor+0x6CD |
| `FFX_AtelSys_Btl_245_resi` | 0x3A2FC0 | an ATEL script query |
| `sub_7A8B40` | 0x3A8B40 | an AI-side query |
| `FFX_BtlMenu_ClassifyCommand` | 0x49ACA0 | greys out the menu row and shows the cost |

The actual subtraction is in exactly one place, **`FFX_Btl_SpendCommandCost 0x38E5A0`**:

```c
actor[0x5D4] = clamp(actor[0x5D4] - actor[0x6CC], 0, 9999);         // MP
actor[0x5BC] = clamp(actor[0x5BC] - actor[0x6CD], 0, actor[0x5BD]); // overdrive
```

It spends the **cached** values, and the cache is filled by `FFX_Btl_CheckCommandCost` from
`FFX_Btl_GetCommandMpCost`. So a 0 return propagates all the way to a 0 subtraction. No other site
subtracts MP as a cost. The only other MP writers in the binary are
`FFX_Btl_ApplyMpDamage 0x38E3B0` (enemy MP drain, which god mode's invincible flag already covers),
`FFX_Btl_LoadCharRecordIntoActor` and `FFX_Btl_LoadUnitParams` (load-time clamps),
`FFX_Booster_InvincibleRefillUnit`, `FFX_SaveData_SetCharCurrentStats 0x3873D0` and the AI property
setter `0x3B4B70`. Checked by scanning every instruction in `.text` with the displacement 0x5D4.

### 4.2 Call site patch, detour, or flag

**Neither. Set the flag.** `g_ffxBtlDbgMagFree = 1` at RVA 0xD2A901 does the whole job, is side
correct, makes the menu display cost 0 and stop greying rows out, and cannot desync differently from
any other write to that byte.

`workshop::PatchCallSite` would be the right tool if there were no flag, because
`FFX_Btl_GetCommandMpCost` has five callers and two of them are the menu's cosmetic path, which we
would want to keep honest. There is a flag, so the question does not arise. For the record, the
function's prologue is `push ebp / mov ebp, esp / 8B 45 08` with no rel32 in the first six bytes, so
a 5-byte detour would also be clean if one were ever needed.

Separately, free MP does **not** make overdrives free. The gauge requirement is a different branch
and `g_ffxBtlDbgLimitBreakOn 0xD2A90C` is the flag for that. It is read in
`FFX_Btl_GetCommandMpCost`, `FFX_Btl_CheckCommandCost`, `FFX_Btl_GetUsableCommandKinds 0x38F700`
(the menu's "is Overdrive selectable" test) and one AI property site, so one byte covers all four.

## 5. The overdrive gauge

Two copies, and they are not redundant.

```
charRecord+0x39   byte   the persistent gauge, in the save block
battleActor+0x5BC byte   the live gauge, during a battle
```

`FFX_Btl_LoadUnitParams 0x39B4F0` copies record+0x38, +0x39, +0x3A into actor+0x5BB, +0x5BC, +0x5BD
at battle start (instruction sites 0x39B599, 0x39B5A0, 0x39B5A7, with `ebx = actor+0x540`).
`FFX_Btl_CommitActorsToSave 0x385FC0` copies all three back at battle end. So:

* **Out of battle, write charRecord+0x39.** `FFX_Battle_IsActive()` is false and nothing else holds a
  copy.
* **In battle, write battleActor+0x5BC.** Writing the record instead would be silently discarded by
  the end-of-battle writeback.
* A cheat UI that is open in both places should check `FFX_Battle_IsActive()` and pick. The
  precedent for that is `FFX_SaveData__getCharCurrentStats 0x387230`, which reads the actor instead
  of the record whenever a battle is active.

**Range and the full threshold.** Both bytes are unsigned. The gauge is `0..max` where max is the
byte at actor+0x5BD / record+0x3A, and "full" means `gauge == max`, not a fixed number:

* `FFX_Btl_AddOverdrive 0x3B1590` does `actor[0x5BC] = clamp(gauge + gain, 0, actor[0x5BD])`.
* `FFX_Btl_GetCommandMpCost` rejects an overdrive ability while `actor[0x5BC] < actor[0x5BD]`.
* `FFX_Btl_GetUsableCommandKinds 0x38F700` reports the overdrive kind usable while
  `actor[0x5BC] >= actor[0x5BD]`.

**So a cheat must read the max and write that, not a constant.** I could not pin max to a number
from the executable: record+0x3A is initialised wholesale by `FFX_InitNewSaveData 0x386B00`, which
memcpy's each 148-byte record straight out of the `ply_save` blob in the battle kernel table. It is
data, and it is plausibly per character. `FFX_Booster_InvincibleRefillUnit` fills the gauge with
exactly `actor[0x5BC] = actor[0x5BD]`, which is the shipped idiom and the one to copy.

Read helpers, so the UI need not know the offsets: `FFX_Btl_GetOverdriveGauge 0x395550` and
`FFX_Btl_GetOverdriveMax 0x395590`, both `int __cdecl (unsigned char unitIndex)` and both returning
0 for a non-ally index.

**A gauge-always-full flag also exists.** `g_ffxBtlDbgOverdriveAlwaysFull 0xD333E8`, a dword, is read
at the top of `FFX_Btl_GetUsableCommandKinds` and when non-zero it writes
`actor[0x5BC] = actor[0x5BD]` for the unit whose menu is opening. That makes overdrive permanently
available for whoever is in the menu without touching the gauge logic. It is a narrower and tidier
lever than `g_ffxBtlDbgLimitBreakOn`, which bypasses the check instead of satisfying it.

## 6. Overdrive modes

### 6.1 Where the three pieces live

All three are in the 148-byte character record at `g_ffxCharRecords 0xD3205C`, 18 records, and all
three were previously in the "no reader identified" part of `reversing/GAME_STATE.md`.

| off | size | field |
|---|---|---|
| **+0x38** | byte | **the SELECTED mode id, 0..19** |
| **+0x60** | 20 words | **the per-mode use counter. 0xFFFF = this mode is not available to this character, 0 = earned, otherwise uses remaining** |
| **+0x88** | dword | **the UNLOCKED-modes bitmask, bit n = mode id n** |

Proof, each independent of the others:

* `FFX_Btl_BumpOverdriveModeCounter 0x3B10C0`, signature `int __cdecl (unsigned charIndex,
  unsigned modeId, int force)`. Guards `charIndex <= 6` and `modeId <= 0x10`, reads the word at
  `g_ffxCharRecords[148*charIndex + 96 + 2*modeId]`, skips entirely when it is 0xFFFF, decrements it
  toward 0 otherwise, and tests the bit `(record[0x88] >> modeId) & 1`. 96 is 0x60.
* `FFX_Btl_GrantUnlockedOverdriveModes 0x3B1180`, called from `FFX_Btl_MainStep` at 0x390D7E. For
  each of the 7 party-order slots, loops `modeId` from 0 to **19**: if the bit in record+0x88 is
  clear and the counter at record+0x60+2*modeId is 0, shows the "learned" message and sets the bit.
* `FFX_Debug_OverdriveModesOneUseAway 0x384C40`, the shipped debug helper reached from
  `SG_DebugWin_BattleConfigProc`: for all 18 records, sets every **non-zero** one of the **20** words
  at record+0x60 to 1, which is "every still-locked mode is one use away". The 20 and the +0x60 come
  straight out of that loop.
* `FFX_Menu_GetCharOverdriveMode 0x4C1BD0` returns `record[0x38]` as a byte.
  `FFX_Menu_GetCharOverdriveModeMask 0x4C1BF0` returns `record[0x88]` as a dword.
  `FFX_Menu_SetCharRecordByte38 0x4C2C90` writes `record[0x38]`.
* `FFX_Btl_LoadUnitParams` copies record+0x38 into actor+0x5BB, and six functions dispatch on
  actor+0x5BB against the constants 0 through 0x13: `0x3B0D50` (modes 0,1,2,3,0x13),
  `0x3B0F80` (7,8,9), `0x3B1090` (0xC), `0x3B12C0` (4,5,6,0xA),
  `FFX_Btl_PlayTurnStartVoice 0x3B13C0` (0xD,0xE,0xF,0x10), `0x3B1540` (0xB). Together they account
  for 0..0x10 plus 0x13. The constant 20 appears four separate times, so **the mode count is 20,
  ids 0..19**, and I am confident in that.

The counter path caps `modeId` at 0x10 while the grant path walks to 19, so the top three modes
(0x11, 0x12, 0x13) have no use counter path in the executable. Either they are unlocked from the
start for the characters that have them, or their counters are driven from elsewhere. I did not
settle which, and it does not matter for a cheat that sets the mask directly.

### 6.2 The mode list, with display names

The Overdrive screen is menu module 21, exec `FFX_Menu_ExecModule21Overdrive 0x4D0460`. Its mode
list is built by `FFX_Menu_BuildListRows 0x4C2390` case 1, which is 11 lines:

```c
case 1:
  mask = FFX_Menu_GetCharOverdriveModeMask(FFX_Menu_GetCursorChar());   // record+0x88
  for (j = 0; j < 20; ++j) {
      modeId = g_ffxOverdriveModeDisplayOrder[j];
      if ((1 << modeId) & mask) {
          row->id   = modeId;
          row->kind = 3;        // the word at row+2 is 259 = 0x0103
      }
  }
```

* **`g_ffxOverdriveModeDisplayOrder` RVA 0x88765C**, 20 bytes, the ids in screen order:
  `2, 0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19`. Its only cross reference
  in the whole binary is this loop.
* **The display NAME is `FFX_KernelString_Get(1, 4147 + modeId, lang)` RVA 0x38FCF0.** From
  `FFX_Menu_Od_DrawListRow 0x4D1820` case 3, which does
  `FFX_Menu_GetKernelString1Name(rowId + 4147)` and draws the result. 4147 is 0x1033.
* **The description is `FFX_KernelString_GetDescription(1, 4147 + modeId, lang)` RVA 0x38FBB0.** From
  `0x4D1A20` case 3, which feeds `FFX_Menu_SetHelpString`.
* `lang` is `sub_7851F0()` RVA 0x3851F0, which is `(*(DWORD*)0xD3079C >> 3) & 1`. Call it, do not
  reimplement it.
* The returned bytes are in the FFX glyph encoding, not ASCII. See the note on
  `FFX_Text_DrawString 0x505AB0` in the IDB. The plugin needs the same decode it already uses for
  other kernel strings.
* Out-of-range ids are **safe**: `FFX_KernelTable_GetRow 0x3AB870` falls back to the first range
  descriptor rather than computing a wild pointer, so probing 4147..4166 cannot fault.

I did not read the 20 name strings, because they are not in the executable. They live in the kernel
string blob loaded from a data file, which means **the picker has to be populated by calling
`FFX_KernelString_Get(1, 4147 + id, lang)` at runtime**, which is what the brief asked for anyway.
The familiar English names (Stoic, Healer, Comrade, Tactician, Victim, Dancer, Avenger, Slayer,
Rook, Victor, Loner, Ally, Daredevil, Solitude, ...) are what those 20 ids resolve to, but I am not
asserting a specific id-to-name mapping from the exe.

### 6.3 The write sequence

**Unlock everything, one write per character:**

```c
// charIndex 0..17, though only 0..6 are the overdrive-capable party
char *rec = FFX_SaveData__getCharRecord(charIndex);   // 0x385330
*(DWORD *)(rec + 0x88) = 0x000FFFFF;                   // bits 0..19
```

That is sufficient, and it is safe. The menu list reads record+0x88 and nothing else, and
`FFX_Btl_GrantUnlockedOverdriveModes` only fires for a mode whose bit is **clear**, so setting all 20
bits also suppresses the "learned a new overdrive mode" popups rather than triggering 140 of them.
Leave the counters at record+0x60 alone: 0xFFFF there means "not for this character" and zeroing
them would be meaningless once the bits are set.

If instead you want the game to grant them naturally, `FFX_Debug_OverdriveModesOneUseAway 0x384C40`
is the shipped one-call version: every in-progress counter becomes 1.

**Select a mode:**

```c
FFX_Menu_SetCharRecordByte38(charIndex, modeId);   // 0x4C2C90, writes record+0x38
if (FFX_Battle_IsActive())
    *(BYTE *)(FFX_Battle_GetActor(charIndex) + 0x5BB) = modeId;   // the live copy
```

The second line matters. Without it an in-battle selection does nothing until the next battle, and
worse, `FFX_Btl_CommitActorsToSave` will copy the stale actor+0x5BB back over record+0x38 at battle
end and undo the change.

## 7. Per-character overdrive abilities

### 7.1 Where "learned" lives

`FFX_SaveData_SetCharAbilityFlag 0x385C50` is `int __cdecl (int charIndex, int abilityId, int on)`
and it is the one function to call. It refuses anything whose high nibbles are not 0x3000, then
splits:

* `abilityId < 0x3060`: the per-character bitmap inside the record, word at
  `g_ffxCharRecords[148*charIndex + 62 + 2*((id & 0xFFF) / 16)]`, bit `id & 0xF`. 62 is 0x3E, which
  matches the "+0x3E..0x49, 6 words" row already in `GAME_STATE.md`.
* `abilityId >= 0x3060`: a **shared, not per-character** bitmap at `0xD307FC`, word
  `((id - 96) & 0xFFF) / 16`, bit `(id - 96) & 0xF`. `charIndex` is ignored on this branch.

**Every overdrive ability id is 0x3060 or above**, so learned overdrives are global to the save, not
per character. That is consistent: each id belongs to exactly one character anyway. The id 0x3060
maps to word 0, bit 0 of that bitmap.

The function then calls `FFX_Battle_IsActive()` and, if a battle is running, re-syncs the actor's
usable-ability state through `0x39BB50`. **That is why you call it instead of setting the bit
yourself:** one call covers the save block and the live battle actor.

The matching test is `maybe_FFX_SaveData_TestEventFlag 0x385020`, same two branches.

The engine itself uses exactly this pair to teach Kimahri a Ronso Rage: in
`FFX_Btl_ApplyPendingEffects`, when the target is unit 3 and the entry's flag byte has bit 2, it
tests the id, calls `FFX_SaveData_SetCharAbilityFlag(3, id, 1)`, shows the message, and sets
`actor+0x5BC = actor+0x5BD` to fill his gauge. So granting an ability and filling the gauge together
is a shipped sequence, not something we invented.

There is a second, wider ability bitmap at `saveData+0x6034` with its own setter
`FFX_SaveData_SetCharAbility 0x385E00` (already in `addresses/GameState.h`). `GAME_STATE.md` flags
that it never established which of the two means what. The overdrive path uses 0x385C50, so that is
the one to call for this feature.

### 7.2 The id list with names

The id ranges are already established in `reversing/OVERDRIVES.md` section 11 from the shipped
`command.bin` kind bytes, and I did not re-derive them:

| Character | unit | overdrive ability ids | count |
|---|---|---|---|
| Tidus | 0 | 0x3060..0x3063 | 4 |
| Yuna | 1 | - (aeons, not abilities) | - |
| Auron | 2 | 0x3064..0x3067 | 4 |
| Kimahri | 3 | 0x3068..0x3073 | 12 |
| Wakka | 4 | 0x3074..0x3077 | 4 |
| Lulu | 5 | 0x3078..0x308A | 19 |
| Rikku | 6 | 0x308B..0x30CA | 64 |
| parent commands | - | 0x3118..0x311E | 7 |

**The name for any id is `FFX_Menu_GetAbilityName(id)` RVA 0x4C1A20**, which is three lines:

```c
char *row = FFX_Btl_GetPlayerAbilityRow(id, &stringBase);   // 0x390A90
return stringBase + *(unsigned short *)(row + 4 * lang);    // lang = sub_7851F0()
```

The help line is `FFX_Menu_GetAbilityHelp 0x4C19E0`, the same row read at `+8 + 4*lang`. Both return
FFX glyph-encoded bytes. An id with no row still returns a valid pointer, to the wrong string, so
the UI should also check the row itself rather than trusting a non-null return.

### 7.3 Enumerating without hardcoding the ranges

If the UI is to build its list from data rather than from the table above, the same source the
Abilities and Overdrive screens use is walkable:

```c
int   stringBase;
char *table = FFX_Btl_GetMenuAbilityTableRow(0, &stringBase);   // 0x390200, kernel table 0xD2A958
// 4 bytes per entry: { u8 charIndex, u8 group, u16 abilityId }, terminated by charIndex == 0xFF
for (char *e = table; (unsigned char)e[0] != 0xFF; e += 4) {
    if ((unsigned char)e[0] != wantedChar) continue;
    unsigned short id = *(unsigned short *)(e + 2);
    bool known = maybe_FFX_SaveData_TestEventFlag(wantedChar, id);   // 0x385020
    const char *name = (const char *)FFX_Menu_GetAbilityName(id);    // 0x4C1A20
}
```

Two helpers exist if you want the engine to do the scanning:
`FFX_Menu_CountTableEntries 0x4D2D80` is `int (base, dummy, charIndex, group)` and returns the match
count, `FFX_Menu_FindTableEntry 0x4D2DC0` is the same arguments and returns the first matching index.
The `dummy` argument is only tested against 0, pass 5 as the engine does. Group 72 (0x48) selects a
character's overdrive PARENT command, which is how the Overdrive screen's header gets
"Swordplay" / "Bushido" / "Slots" / "Fury" / "Ronso Rage" / "Mix".

**Confidence:** the entry stride of 4, the terminator and the field positions are read off the two
helper functions and off `FFX_Menu_BuildListRows` case 2, so those I am sure of. What the `group`
byte means beyond "0 is the general ability list and 72 is the overdrive parent" I did not establish.

## 8. Recommendations

### 8.1 God mode

**Build it as two flag writes plus one optional detour.**

```c
*(BYTE *)rva(0xD2A8F9) = enabled ? 1 : 0;   // g_ffxBtlDbgPlyInvincible, no HP or MP damage
*(BYTE *)rva(0xD2A901) = enabled ? 1 : 0;   // g_ffxBtlDbgMagFree, free casting
*(BYTE *)rva(0xD2A90C) = enabled ? 1 : 0;   // g_ffxBtlDbgLimitBreakOn, overdrive always usable
```

That is the whole feature, it needs no code patch, and it uses the engine's own side test so it
cannot accidentally protect enemies. The reason to prefer it over a detour is not elegance, it is
that `FFX_Btl_IsUnitDamageable` is called in three places and the flag covers all three, whereas a
detour on one applier would miss the others.

Add the detour in 8.3 **only if** statuses must also be blocked for the party. The invincible flag
does not stop Sleep, Confuse or the death bit, which is a real gap: a Death-casting enemy can still
remove an "invincible" party member.

### 8.2 Instant kill

**Build it as a flag plus a button, not as a damage override.**

```c
*(BYTE *)rva(0xD2A91E) = enabled ? 1 : 0;   // g_ffxBtlDbgMonHp1, every enemy enters battle at 1 HP
```

and bind a "kill everything now" button to the shipped helper, so it also works in a battle that
already started:

```c
// void __cdecl FFX_Debug_SetAllMonsterHp(int toOne)
typedef void (__cdecl *SetAllMonsterHp)(int);
((SetAllMonsterHp)rva(0x384BC0))(1);        // units 20..27 current HP = 1, HUD refreshed
```

`FFX_Debug_SetAllMonsterHp` checks `FFX_Battle_IsActive()` itself, so it is safe to call
unconditionally from a UI callback.

Why not maximum damage: the cap is 99999 and the fiends people want this for have millions of HP, so
a maximum-damage hit is not a kill. 1 HP is a kill, always, and it leaves the bestiary, overkill,
drops, AP and the death animations working normally because the unit really did die of damage.

If the user specifically wants to **see** the big number rather than win, add
`*(BYTE *)rva(0xD2A910) = 1` (`g_ffxBtlDbgDmgIs100000`). Flag it in the UI as cosmetic and as **not
side selective** - it makes incoming attacks do the maximum too, which is only survivable with
PlyInvincible on at the same time. If a side-selective version is wanted, that is the detour in 8.3.

### 8.3 The one detour that covers both, if the flags are not enough

Detour `FFX_Btl_ComputeEffectForTarget 0x38E630` and fix the entry up after the original returns.
This is the single best hook point for both "take zero damage" and "deal the maximum", because it is
the one place where the numbers exist as plain dwords, the attacker and the target are both in the
argument list, and nothing has been applied yet.

```c
// int __cdecl FFX_Btl_ComputeEffectForTarget(
//     int atkIndex, int atkActor, int tgtIndex, int tgtActor,
//     int abilityRow, int a6, unsigned char *entry44,
//     int a8, int a9, int a10, int *outExtra);

int __cdecl Hook_ComputeEffect(int atkIdx, int atkActor, int tgtIdx, int tgtActor,
                               int abilityRow, int a6, unsigned char *entry,
                               int a8, int a9, int a10, int *outExtra)
{
    int r = Original(atkIdx, atkActor, tgtIdx, tgtActor, abilityRow, a6, entry,
                     a8, a9, a10, outExtra);

    int *dmg = (int *)(entry + 0x20);           // [0] HP, [1] MP, [2] CTB

    if (g_godMode && !FFX_Btl_IsEnemySlot(tgtIdx))      // 0x39AEF0, enemies are 20..27
    {
        if (dmg[0] > 0) dmg[0] = 0;             // keep healing, kill damage
        if (dmg[1] > 0) dmg[1] = 0;
        dmg[2] = 0;                             // no CTB delay either
        *(unsigned short *)(entry + 0x14) = 0;  // status word 1
        *(unsigned short *)(entry + 0x16) = 0;  // status word 2, this is where the death bit is
        memset(entry + 0x07, 0, 13);            // status turn counters
    }
    else if (g_instantKill && FFX_Btl_IsEnemySlot(tgtIdx) && !FFX_Btl_IsEnemySlot(atkIdx))
    {
        if ((*(unsigned short *)(entry + 0x18) & 1) != 0 && dmg[0] > 0)
            dmg[0] = (*(unsigned short *)(atkActor + 0x6BE) & 0x800) ? 99999 : 9999;
    }

    return dmg[0];      // the original returns entry+0x20, and the caller sums it
}
```

Three things to be careful about, all verified:

* **Return `dmg[0]`, not `r`.** `FFX_Btl_StageEffectsForTarget` adds the return value into a running
  total that becomes the predicted-death byte at tgtActor+0xDEC and the overkill test. Returning the
  pre-fixup value makes the HUD and the death prediction disagree with what actually happens.
* **Respect the resource mask at entry+0x18.** Writing a damage value into a slot whose mask bit is
  clear does nothing, because `FFX_Btl_ApplyPendingEffects` tests the bit before applying. Setting
  the bit yourself would make a pure-status ability suddenly deal damage.
* **Sign matters.** Positive is damage, negative is healing, and the clamp is symmetric. Zeroing
  unconditionally would break Cure on the party, which is why the god-mode branch only zeroes
  positive values.

The prologue is a normal `push ebp / mov ebp, esp / sub esp, 0xB8` with no rel32 in the first five
bytes, so a plain 5-byte detour is clean here. That was checked.

### 8.4 Free MP

Covered in 8.1: `g_ffxBtlDbgMagFree 0xD2A901`. One byte, side correct because enemies already pay
nothing, no call site patch and no detour. `workshop::PatchCallSite` is not needed and would be worse
here, because the menu's cost display goes through the same function and the flag keeps the display
and the behaviour consistent.

### 8.5 Overdrive editing

```c
// Gauge. Read the max, never assume a constant.
if (FFX_Battle_IsActive()) {
    BYTE *a = (BYTE *)FFX_Battle_GetActor(unitIndex);      // 0x394020
    a[0x5BC] = clampByte(value, 0, a[0x5BD]);
} else {
    char *rec = FFX_SaveData__getCharRecord(charIndex);    // 0x385330
    rec[0x39] = (char)clampByte(value, 0, (BYTE)rec[0x3A]);
}

// Unlock every mode.
*(DWORD *)(FFX_SaveData__getCharRecord(charIndex) + 0x88) = 0x000FFFFF;

// Select a mode.
FFX_Menu_SetCharRecordByte38(charIndex, modeId);           // 0x4C2C90
if (FFX_Battle_IsActive())
    ((BYTE *)FFX_Battle_GetActor(charIndex))[0x5BB] = (BYTE)modeId;

// Grant an overdrive ability. One call does the save block and the live actor.
FFX_SaveData_SetCharAbilityFlag(charIndex, abilityId, 1);  // 0x385C50
```

Populate the mode picker from `g_ffxOverdriveModeDisplayOrder 0x88765C` (20 bytes, display order)
with the label from `FFX_KernelString_Get(1, 4147 + id, sub_7851F0())` and the tooltip from
`FFX_KernelString_GetDescription(1, 4147 + id, ...)`. Populate the overdrive-ability picker from the
menu ability table walk in 7.3, or from the id ranges in 7.2, with the label from
`FFX_Menu_GetAbilityName 0x4C1A20`.

## 9. The full debug flag block

48 adjacent bytes of `.data`, all zero at startup. Writers in the retail binary are
`SG_DebugWin_BattleConfigProc 0x3C6E20` (the debug window),
`FFX_AtelSys_Btl_125_resi 0x3A81F0` (an ATEL set-by-index syscall),
`FFX_AtelSys_Btl_126_resi 0x3A27E0` (the matching get) and `0x3CE450` (a bulk reset). Nothing gates
them on `g_ffxDebugMode`, so a plugin can just write them. Listed because several are useful beyond
the three in the headline, and because knowing which ones exist stops us detouring code that already
has a switch.

| RVA | name | effect, and where it is read |
|---|---|---|
| 0xD2A8E1 | BtlDbgCtbPause | freezes the CTB clock. Already in Battle.h |
| 0xD2A8F5 | BtlDbgAutoExecute | already in Battle.h |
| 0xD2A8F8 | BtlDbgMonInvincible | enemies take no HP or MP damage. `FFX_Btl_IsUnitDamageable` |
| 0xD2A8F9 | BtlDbgPlyInvincible | party takes no HP or MP damage. Same function |
| 0xD2A8FA | BtlDbgMonInput | AI units open the player menu. Already in Battle.h |
| 0xD2A8FB | BtlDbgCtbSameOrder | CTB ordering |
| 0xD2A900 | BtlDbgMagNotEff | magic has no effect |
| **0xD2A901** | **BtlDbgMagFree** | **MP cost becomes 0. `FFX_Btl_GetCommandMpCost 0x38C690`** |
| 0xD2A902 | BtlDbgMagNum0 | `FFX_Btl_BeginMagicEffect 0x387F20`, `FFX_Btl_EndPhaseStep` |
| 0xD2A904 | BtlDbgSumNotEff | summons have no effect |
| 0xD2A905 | BtlDbgFullSet | `FFX_Btl_Init 0x381700` |
| 0xD2A906 | BtlDbgDmgStatusOff | skips all status application, both sides. `FFX_Btl_ComputeEffectForTarget` |
| 0xD2A907 | BtlDbgExchgWillDie | `0x3B2DC0`, an AI property path |
| 0xD2A908 | BtlDbgDmgRandomOff | removes damage variance. Passed to `FFX_Btl_DamageFormula` |
| 0xD2A909 | BtlDbgDmgCritOff | no criticals |
| 0xD2A90A | BtlDbgDmgProbOff | no probability rolls |
| 0xD2A90B | BtlDbgPrintInfo | already in Battle.h |
| **0xD2A90C** | **BtlDbgLimitBreakOn** | **overdrive abilities usable at any gauge. Four readers** |
| 0xD2A90D | BtlDbgDmgCritOn | always critical |
| 0xD2A90E | BtlDbgDmgIs1 | every HP and MP damage becomes 1, before the clamp |
| 0xD2A90F | BtlDbgDmgIs10000 | becomes 10000, clamps to 9999 |
| **0xD2A910** | **BtlDbgDmgIs100000** | **becomes 100000, clamps to 9999 or 99999** |
| 0xD2A911 | - | `0x38B760`, `0x3990D0` |
| 0xD2A912 | - | `0x3990D0` |
| 0xD2A913 | - | `0x3990D0` |
| 0xD2A914 | BtlDbgOverKillOff | suppresses the Overkill display. `FFX_Btl_OnUnitDefeated` |
| 0xD2A915 | BtlDbgEveryLook | camera |
| 0xD2A916 | BtlDbgLimitBreakOff | **stops the gauge filling at all. `FFX_Btl_AddOverdrive 0x3B1590`** |
| 0xD2A917 | BtlDbgTestHitEff | |
| 0xD2A918 | BtlDbgDvdSlowStart | |
| 0xD2A91A | BtlDbgThrowAwayParm | |
| 0xD2A91B | BtlDbgMapCircle | |
| 0xD2A91C | BtlDbgDvdStopMag | |
| 0xD2A91D | BtlDbgPlyHp1 | party loads at 1 HP. `FFX_Btl_LoadUnitParams 0x39B74D` |
| **0xD2A91E** | **BtlDbgMonHp1** | **enemies load at 1 HP. Same function, 0x39BAF6** |
| 0xD2A91F | BtlDbgDmgHitMiss | |
| 0xD2A920 | BtlDbgWeapon | |
| 0xD2A921 | BtlDbgMagicItem | |
| 0xD2A922 | BtlDbgSkipCommand | already in Battle.h |
| 0xD2A924..27 | camera and look flags | |

Three more that are not in that block:

| RVA | name | effect |
|---|---|---|
| 0xD3338C | BtlDbgKillAllHp | dword. Every HP damage event first does `curHp -= 99999`, all units, with units whose actor+4 is 1..7 exempt. `FFX_Btl_ApplyHpDamage`. Set by `0x38E620`, called from `0x2B81D0` |
| 0xD333E8 | BtlDbgOverdriveAlwaysFull | dword. Fills the menu-opening unit's gauge to max. `FFX_Btl_GetUsableCommandKinds 0x38F700`. Set by `0x390420` |
| 0xD33390 / 0xD33394 | BtlDealt9999Flag / BtlDealt99999Flag | dwords. Latched by `FFX_Btl_StageEffectsForTarget` when a single hit reaches the cap. Almost certainly the achievement trackers |

And four shipped debug functions worth binding to buttons:

| RVA | signature | what it does |
|---|---|---|
| 0x384B00 | `void (void)` | `FFX_Debug_MaxHpMpAndStats`. All 18 records: HP 99999, MP 9999, every stat 0xFF |
| 0x384B80 | `void (void)` | `FFX_Debug_AllUnitsHp1`. All 31 battle units to 1 HP |
| 0x384BC0 | `void (int toOne)` | `FFX_Debug_SetAllMonsterHp`. Units 20..27 to 1 HP, or back to max |
| 0x384C80 | `void (int toOne)` | `FFX_Debug_SetAllPartyHp`. Units 0..17 to 1 HP, or back to max. Argument 0 is a free full heal |
| 0x384C40 | `void (void)` | `FFX_Debug_OverdriveModesOneUseAway` |

## 10. Address table

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| 0x00389740 | func | BtlStageEffectsForTarget | `int (int atkIdx, int atkActor, int tgtIdx, int tgtActor, int abilityRow, int a6, u8 *block, int, int, int, int)` | snapshots HP/MP/CTB into the working trio, loops the entries |
| 0x0038E630 | func | BtlComputeEffectForTarget | `int (int atkIdx, int atkActor, int tgtIdx, int tgtActor, int abilityRow, int a6, u8 *entry44, int, int, int, int *out)` | THE formula, the cap, and the best single hook |
| 0x0038F060 | func | BtlApplyPendingEffects | `int (int atkIdx, int a2, int tgtIdx, int *a4, short flags)` | applies the parked entries |
| 0x0038D980 | func | BtlEffectCompleteStep | `int (...)` | the only caller of ApplyPendingEffects |
| 0x0038E230 | func | BtlApplyHpDamage | `int (int tgtIdx, int *tgtActor, int amount, int other, int a5, int a6, int a7)` | actor+0x5D0 -= amount |
| 0x0038E3B0 | func | BtlApplyMpDamage | `int (int tgtIdx, int tgtActor, int amount, int other, int a5, int a6, int a7)` | actor+0x5D4 -= amount |
| 0x0038E1E0 | func | BtlApplyCtbDelay | `int (int tgtIdx, int tgtActor, int amount, int a4, int a5, int a6)` | actor+0x65C += amount, not gated by invincible |
| 0x0038D3A0 | func | BtlIsUnitDamageable | `BOOL (int unitIdx)` | the engine's own invincibility gate |
| 0x00389BF0 | func | BtlDamageFormula | `int (int atkActorIdx, int tgtActor, int abilityRow, int a4, int a5, short a6, int slot, int useVariance, int *, char **, int)` | the raw number |
| 0x0039A0C0 | func | ClampInt | `int (int v, int lo, int hi)` | used everywhere, including both HP clamps |
| 0x0039FA00 | func | BtlShowFloatingNumber | `int (char unitIdx, int kind, int value, int other, int a5, int a6, char a7)` | kind 0 HP, 1 MP, 2 CTB, 4 miss, 5 overkill |
| 0x0038C690 | func | BtlGetCommandMpCost | `int (u8 actorIdx, int abilityRow, int extraCost)` | already in Battle.h |
| 0x0038CF70 | func | BtlGetRawMpCost | `int (u8 actorIdx, int abilityRow)` | ability row +37, with the MP auto-abilities applied |
| 0x0038E5A0 | func | BtlSpendCommandCost | `int (int actorIdx)` | the ONLY cost-side MP subtraction |
| 0x0038F700 | func | BtlGetUsableCommandKinds | `int (int actorIdx)` | bit 1 normal, bit 2 overdrive. Reads BtlDbgOverdriveAlwaysFull |
| 0x0039AEF0 | func | BtlIsEnemySlot | `BOOL (u8 unitIdx)` | `(idx - 20) <= 7`. Declared in addresses/GameState.h |
| 0x003B1590 | func | BtlAddOverdrive | `int (int charIdx, int actor, int amount)` | gauge += scaled, clamped to actor+0x5BD |
| 0x003B10C0 | func | BtlBumpOverdriveModeCounter | `int (unsigned charIdx, unsigned modeId, int force)` | decrements record+0x60+2*modeId |
| 0x003B1180 | func | BtlGrantUnlockedOverdriveModes | `void (void)` | from FFX_Btl_MainStep, sets the record+0x88 bits |
| 0x00395550 | func | BtlGetOverdriveGauge | `int (u8 unitIdx)` | actor+0x5BC, 0 for a non-ally |
| 0x00395590 | func | BtlGetOverdriveMax | `int (u8 unitIdx)` | actor+0x5BD, 0 for a non-ally |
| 0x003955F0 | func | BtlGetOverdriveMode | `int (u8 unitIdx)` | actor+0x5BB |
| 0x0039C5F0 | func | BtlLoadCharRecordIntoActor | `int (int charIdx)` | record -> actor, and where actor+0x6BC/0x6BE/0x6C0 are built from equipment |
| 0x0039B4F0 | func | BtlLoadUnitParams | `int (int a1, int unitIdx)` | record+0x38/0x39/0x3A -> actor+0x5BB/0x5BC/0x5BD, and the two Hp1 flags |
| 0x00385FC0 | func | BtlCommitActorsToSave | `char (void)` | actor -> record at battle end, including the overdrive trio |
| 0x0038C740 | func | BtlOnUnitDefeated | `int (int killerIdx, int victimIdx, int a3, int a4)` | declared in addresses/GameState.h |
| 0x003847C0 | func | BoosterInvincibleRefillUnit | `void *(int unitIdx)` | the shipped Invincible booster's per-turn top-up |
| 0x00384B00 | func | DebugMaxHpMpAndStats | `void (void)` | HP 99999, MP 9999, stats 0xFF, all 18 records |
| 0x00384B80 | func | DebugAllUnitsHp1 | `void (void)` | all 31 units to 1 HP |
| 0x00384BC0 | func | DebugSetAllMonsterHp | `void (int toOne)` | units 20..27, 1 HP or max |
| 0x00384C80 | func | DebugSetAllPartyHp | `void (int toOne)` | units 0..17, 1 HP or max |
| 0x00384C40 | func | DebugOverdriveModesOneUseAway | `void (void)` | every in-progress mode counter to 1 |
| 0x00385C50 | func | SaveDataSetAbilityFlag | `int (int charIdx, int abilityId, int on)` | grants an ability and re-syncs the live actor |
| 0x00385020 | func | SaveDataTestAbilityFlag | `BOOL (int charIdx, int abilityId)` | the matching test |
| 0x003850B0 | func | SaveDataTestInnateAbilityFlag | `BOOL (int abilityId)` | the "always available" table |
| 0x0038FCF0 | func | KernelStringGet | `const char *(int group, int id, char lang)` | group 1 id 4147+modeId is an overdrive mode NAME |
| 0x0038FBB0 | func | KernelStringGetDescription | `const char *(int group, int id, char lang)` | the same row's description |
| 0x003851F0 | func | SaveDataGetStringLang | `int (void)` | `(*(DWORD*)0xD3079C >> 3) & 1` |
| 0x00390200 | func | BtlGetMenuAbilityTableRow | `char *(int index, int *outStringBase)` | kernel table at 0xD2A958 |
| 0x004C1A20 | func | MenuGetAbilityName | `const char *(short abilityId)` | row + 4*lang, glyph encoded |
| 0x004C19E0 | func | MenuGetAbilityHelp | `const char *(short abilityId)` | row + 8 + 4*lang |
| 0x004C1BB0 | func | MenuGetListRowCount | `int (void)` | reads MenuListRowCount |
| 0x004C1BD0 | func | MenuGetCharOverdriveMode | `int (u8 charIdx)` | record+0x38 |
| 0x004C1BF0 | func | MenuGetCharOverdriveModeMask | `int (u8 charIdx)` | record+0x88 |
| 0x004C2390 | func | MenuBuildListRows | `int (int kind, int a2)` | case 1 is the overdrive mode list |
| 0x004C2360 | func | MenuBuildListRowsKind | `int (int kind)` | the one-argument wrapper |
| 0x004C2C90 | func | MenuSetCharRecordByte38 | `char *(u8 charIdx, char modeId)` | declared in addresses/MenuSystem.h |
| 0x004D0460 | func | MenuExecModule21Overdrive | `void (...)` | declared in addresses/MenuSystem.h |
| 0x004D0A20 | func | MenuOdBuildScreen | `int (int panel, int a2)` | module 21's screen setup |
| 0x004D0D50 | func | MenuOdCreateAbilityPanel | `int (int charIdx)` | the left panel |
| 0x004D0DD0 | func | MenuOdCreateModePanel | `int (int charIdx)` | the right panel, the mode list |
| 0x004D1820 | func | MenuOdDrawListRow | `void (int panel, float x, float y, int row, int a5)` | case 3 draws a mode name |
| 0x004D1A20 | func | MenuOdSetRowHelp | `int (int panel)` | case 3 sets a mode description |
| 0x004D2D80 | func | MenuCountTableEntries | `int (u8 *base, int dummy, int charIdx, int group)` | pass 5 as dummy |
| 0x004D2DC0 | func | MenuFindTableEntry | `int (u8 *base, int dummy, int charIdx, int group)` | first matching index |
| 0x0038ECCA | site | BtlDmgCapSelectSite | `mov eax, 800h` | the start of the cap selection |
| 0x0038ECE3 | site | BtlDmgCapBdlConstSite | imm32 90000 | inside `and ebx, 15F90h` |
| 0x0038ECE9 | site | BtlDmgCapBaseConstSite | imm32 9999 | inside `add ebx, 270Fh` |
| 0x0038ED78 | site | BtlDmgClampLoopSite | loop head | clamps the three slots to the cap |
| 0x0038E620 | func | BtlDbgSetKillAllHp | `int (int)` | the only writer of BtlDbgKillAllHp |
| 0x00390420 | func | BtlDbgSetOverdriveAlwaysFull | `int (int)` | the only writer of BtlDbgOverdriveAlwaysFull |
| 0x00D2A8F8 | data | BtlDbgMonInvincible | byte | already in Battle.h |
| 0x00D2A8F9 | data | BtlDbgPlyInvincible | byte | already in Battle.h. GOD MODE |
| 0x00D2A900 | data | BtlDbgMagNotEff | byte | |
| 0x00D2A901 | data | BtlDbgMagFree | byte | FREE MP |
| 0x00D2A902 | data | BtlDbgMagNum0 | byte | |
| 0x00D2A904 | data | BtlDbgSumNotEff | byte | |
| 0x00D2A905 | data | BtlDbgFullSet | byte | |
| 0x00D2A906 | data | BtlDbgDmgStatusOff | byte | blocks statuses, both sides |
| 0x00D2A907 | data | BtlDbgExchgWillDie | byte | |
| 0x00D2A908 | data | BtlDbgDmgRandomOff | byte | |
| 0x00D2A909 | data | BtlDbgDmgCritOff | byte | |
| 0x00D2A90A | data | BtlDbgDmgProbOff | byte | |
| 0x00D2A90C | data | BtlDbgLimitBreakOn | byte | overdrive usable at any gauge |
| 0x00D2A90D | data | BtlDbgDmgCritOn | byte | |
| 0x00D2A90E | data | BtlDbgDmgIs1 | byte | |
| 0x00D2A90F | data | BtlDbgDmgIs10000 | byte | |
| 0x00D2A910 | data | BtlDbgDmgIs100000 | byte | MAXIMUM DAMAGE, not side selective |
| 0x00D2A914 | data | BtlDbgOverKillOff | byte | |
| 0x00D2A916 | data | BtlDbgLimitBreakOff | byte | stops the gauge filling |
| 0x00D2A91D | data | BtlDbgPlyHp1 | byte | party loads at 1 HP |
| 0x00D2A91E | data | BtlDbgMonHp1 | byte | INSTANT KILL |
| 0x00D2A91F | data | BtlDbgDmgHitMiss | byte | |
| 0x00D2A920 | data | BtlDbgWeapon | byte | |
| 0x00D2A921 | data | BtlDbgMagicItem | byte | |
| 0x00D3338C | data | BtlDbgKillAllHp | dword | -99999 on every HP damage event |
| 0x00D333E8 | data | BtlDbgOverdriveAlwaysFull | dword | |
| 0x00D33390 | data | BtlDealt9999Flag | dword | |
| 0x00D33394 | data | BtlDealt99999Flag | dword | |
| 0x0088765C | data | OverdriveModeDisplayOrder | u8[20] | mode ids in screen order |
| 0x00D2A944 | data | BtlAbilityEffectTable | short * | the equipment auto-ability table |
| 0x00D2A958 | data | BtlMenuAbilityTable | short * | the per-character ability list table |
| 0x00D2A92C | data | BtlPlayerAbilityTable | short * | the table BtlGetPlayerAbilityRow uses |
| 0x00D307FC | data | SaveAbilityFlagsShared | u16[] | the bitmap for ability ids 0x3060 and up |
| 0x00D3205C | data | CharRecords | 18 x 148 | declared in addresses/GameState.h |
| 0x01197770 | data | MenuListRows | dword[512] | the shared menu list row array, `{ u16 id, u8 kind, u8 flag }` |
| 0x0146A24C | data | MenuListRowCount | int | how many rows MenuBuildListRows produced |

## 11. Enumeration table

One row per list the cheat UI needs.

| List | Table | Stride | Count, and where it comes from | Display label |
|---|---|---|---|---|
| **Overdrive modes** | `OverdriveModeDisplayOrder` RVA 0x88765C, a `u8[20]` of mode ids in screen order | 1 byte | **20**, fixed. Confirmed four times: the `j < 20` loop in `MenuBuildListRows` case 1, the `< 20` loop in `BtlGrantUnlockedOverdriveModes`, the 20-iteration loop in `DebugOverdriveModesOneUseAway`, and the 20-word counter array at record+0x60 ending exactly where record+0x88 begins | `KernelStringGet(1, 4147 + modeId, SaveDataGetStringLang())`, RVA 0x38FCF0. Tooltip from `KernelStringGetDescription` RVA 0x38FBB0. FFX glyph encoding |
| **A character's unlocked modes** | `CharRecords + 148*charIdx + 0x88`, one dword | - | 20 bits, bit n = mode id n. `MenuGetCharOverdriveModeMask 0x4C1BF0` reads it | as above, filtered by the bit |
| **Mode unlock progress** | `CharRecords + 148*charIdx + 0x60` | 2 bytes | 20 entries, index = mode id. 0xFFFF = not available to this character, 0 = earned, else uses remaining | as above |
| **Selected mode** | `CharRecords + 148*charIdx + 0x38`, one byte, and `BattleGetActor(idx) + 0x5BB` in battle | - | single value 0..19 | as above |
| **Overdrive abilities, per character** | walk `BtlGetMenuAbilityTableRow(0, &stringBase)` RVA 0x390200. Entries are `{ u8 charIdx, u8 group, u16 abilityId }` | **4 bytes** | terminated when `charIdx == 0xFF`. For one character and group use `MenuCountTableEntries(base, 5, charIdx, group)` RVA 0x4D2D80 | `MenuGetAbilityName(abilityId)` RVA 0x4C1A20. Help from `MenuGetAbilityHelp` RVA 0x4C19E0 |
| **Overdrive abilities, by id range** | no table, the ranges in section 7.2 | 1 id | Tidus 4, Auron 4, Kimahri 12, Wakka 4, Lulu 19, Rikku 64, parents 7. From `OVERDRIVES.md` section 11, derived from `command.bin` kind bytes | `MenuGetAbilityName(abilityId)` RVA 0x4C1A20 |
| **Learned state of an ability** | ids below 0x3060: record+0x3E, 6 words. ids 0x3060 and up: the SHARED bitmap at RVA 0xD307FC | 2 bytes per 16 ids | - | test with `SaveDataTestAbilityFlag(charIdx, id)` RVA 0x385020, set with `SaveDataSetAbilityFlag(charIdx, id, 1)` RVA 0x385C50 |
| **Battle debug flags** | RVA 0xD2A8F8 to 0xD2A927, contiguous bytes | 1 byte | 48 bytes, names in section 9. The ATEL index-to-flag mapping is the jump table at `FFX_AtelSys_Btl_126_resi 0x3A27E0` | hand-written labels, section 9. There is no name table in the executable |

The last row is the one exception to "build it from game data": the debug flag names come from the
IDB's symbol table, which came from a debug build's strings, not from a table the retail exe can
read. A checkbox list there has to be hardcoded. Everything else in the UI can be built from the
tables above.

## 12. What I verified, what I inferred, what I could not settle

**Verified in the disassembly or decompilation, with the specific site named above:**

* The whole damage chain, function by function, and that `FFX_Btl_ComputeEffectForTarget` is the only
  caller-visible place where the three damage dwords exist before being applied.
* Current HP +0x5D0, max HP +0x594, current MP +0x5D4, max MP +0x598. Each has two independent
  proofs: the load from the character record in `FFX_Btl_LoadUnitParams` / `BtlLoadCharRecordIntoActor`,
  and the use as the subtrahend or the clamp ceiling in the appliers.
* The cap: 9999, 99999 with actor+0x6BE bit 0x800, overridable by ability row +0x20 bits 0x40 / 0x80.
  Read off the instruction stream at 0x38ECCA..0x38ED01, not from pseudocode.
* `g_ffxBtlDbgPlyInvincible` gates the HP and MP writes and nothing else, with all three callers of
  `FFX_Btl_IsUnitDamageable` enumerated.
* `g_ffxBtlDbgMagFree` zeroes the cost, all five callers of `FFX_Btl_GetCommandMpCost` enumerated,
  and `FFX_Btl_SpendCommandCost` confirmed as the only cost-side MP subtraction by scanning every
  instruction in `.text` with displacement 0x5D4.
* `g_ffxBtlDbgMonHp1` stores 1 into the current HP dword at 0x39BAF6.
* record+0x38 selected mode, record+0x39 gauge, record+0x3A max, and the actor copies at +0x5BB,
  +0x5BC, +0x5BD, with the load sites and the writeback sites both named.
* record+0x60 the 20-word counter array and record+0x88 the unlocked mask, three independent proofs.
* 20 overdrive modes, ids 0..19, four independent proofs.
* `g_ffxOverdriveModeDisplayOrder` 0x88765C and its single cross reference.
* The mode name and description come from kernel string group 1 at id 4147 + modeId.
* `FFX_SaveData_SetCharAbilityFlag 0x385C50` and that overdrive ability ids land in the shared
  bitmap, not the per-character one.
* `FFX_Btl_IsEnemySlot` is `(idx - 20) <= 7`, so enemies are 20..27.

**Inferred, and flagged as such:**

* That the 0x100 bit of actor+0x616 is the status FFX calls Death. The bit's role as a non-HP death
  cause is verified, its name is not.
* That `BtlDealt9999Flag` / `BtlDealt99999Flag` are achievement trackers. They are latched and never
  read on the paths I looked at, which is consistent, but I did not find the reader.
* That actor+0x5A4 is specifically "max HP for the overkill test". It is written from record+0x24 and
  read only by the entry bit 0x80 test, which is what overkill is, but I did not chase what else
  reads bit 0x80.
* The `group` byte of the menu ability table beyond "0 is the general list, 72 is the overdrive
  parent command".

**Could not settle:**

* **The numeric value of the overdrive gauge maximum.** record+0x3A is memcpy'd out of the
  `ply_save` kernel blob by `FFX_InitNewSaveData`, so it is data, and plausibly per character. This
  is why the recommendation is to read actor+0x5BD rather than assume a constant. A one-line runtime
  probe would settle it.
* **The 20 overdrive mode names.** Not in the executable. They come back from
  `KernelStringGet(1, 4147 + id, lang)` at runtime, which is the right way for the UI to get them
  anyway, but it means I cannot print the id-to-name table here.
* **Modes 0x11, 0x12, 0x13 have no use-counter path.** `BtlBumpOverdriveModeCounter` caps modeId at
  0x10 while the grant loop walks to 19. Only mode 0x13 appears in the actor+0x5BB dispatch beyond
  0x10, in `0x3B0D50`. So either those three unlock elsewhere or they start unlocked. Does not
  affect the cheat, which sets the mask directly.
* **Whether record+0x37** (read into actor+0x5BA by `FFX_Btl_LoadUnitParams` at 0x39B576, clamped
  0..255) is overdrive related. It sits immediately before the overdrive trio and
  `GAME_STATE.md` lists it as having no reader, which is now wrong, but I did not find what consumes
  actor+0x5BA.
* **Which of the two per-character ability bitmaps means "learned" and which means "usable".**
  `GAME_STATE.md` already flagged this. The overdrive path uses 0x385C50, so the question does not
  block this feature.
