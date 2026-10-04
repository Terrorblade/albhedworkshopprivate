# Monster capture and the Monster Arena

Does capturing a fiend replicate, and who owns the arena's unlock state when two or three people
are playing.

Addresses in the running text are **VA** unless the line says RVA. Base is 0x400000, so
RVA = VA - 0x400000. The address list in section 10 is **RVAs**, as asked.

Everything here comes out of the disassembly, the shipped kernel tables, the shipped per-monster
data files and the shipped event script. Where something is inference it says so. The two places
agents on this project have been burned before (attacker versus victim registers, and which of two
adjacent globals a test reads) were checked at instruction level on purpose, and section 3 shows
the instructions.

---

## 1. Summary

**1. How a capture is recorded, and which GAME_STATE.md region.** One write site, no ambiguity.
`FFX_Btl_OnUnitDefeated 0x78C740` is the only caller of `FFX_SaveData_TryCapture 0x790B30`, which
bumps `g_ffxMonsterCaptureCounts` at `g_ffxSaveData + 0x420C` (`0x1130C9C`), clamped 0..10. That is
inside GAME_STATE.md's **"Monsters" bucket, +0x420C .. +0x448C, 640 bytes**, so the existing
desync detector already covers the whole subject with no new range.

**2. Is the capture decision derived purely from replicated state.** Yes, completely. The
condition is (a) the command's kernel row has bit 0x40000, (b) the attacker actor has the Capture
auto-ability bit, which is OR'd in from `a_ability.bin` at battle load, (c) the script battle-mode
byte is 0, (d) the victim is enemy actor slot 20..27, (e) the victim's capture species index, which
comes from its own shipped data file, is not 255, and (f) the stored count is below 10. No clock,
no RNG, no frame count, no local-only input, no pointer value. **Capture replicates for free under
delayed-input lockstep. That is the headline answer.**

**3. The per-fiend counts, the species list and the unlock thresholds.** The counts are
`BYTE[512]` at `saveData+0x420C`, and **the index is not a monster id** (GAME_STATE.md has that
wrong, see section 12). It is a **capture species index 0..138** that maps one to one onto a table
I named `g_ffxArenaSpeciesTable 0xC86708`, 139 rows of 8 bytes, holding the monster id, an **area
key** and a **species key**. The exe supplies exactly one query over it,
`FFX_Arena_CountSpeciesWithAtLeast 0x872AA0`, reachable only from three ATEL syscalls. **The
unlock thresholds are in the event script, not in code.** I pulled all 27 of them out of
`nagi0700.ebp` and every single one equals the matching row count in the table.

**4. Which menu module, and what is per-screen.** There is **no arena menu module**. Nothing in
the menu subtree reads capture state. The arena is the ordinary **ATEL list-choice window**
(`core:315`, 48 call sites) plus the ordinary **treasure handout syscall** (`core:347`, 38 call
sites), which asks for menu mode 0x10001, module 17. Everything it touches is per-screen and never
per-player, and the single slots are ones DIALOGUE.md and GAME_STATE.md already flag: one global
message-window input focus, one choice object with one cursor, one treasure staging buffer, one
pending menu mode. The arena adds no new single-slot hazard of its own.

**5. Ownership.** One global collection, and the engine has **no notion of who**.
`FFX_SaveData_AddCaptureCount(speciesIndex, delta)` takes no player, no character and no actor.
The only "who" anywhere on the path is `FFX_SaveData_BumpCharBattleCounter(killer, 1)` at
`0x78C854`, a per-character **statistics** counter. So whoever lands the kill, the fiend goes into
the party's single shared collection, and only that player's "enemies defeated" stat moves.

**6. Wall clocks and unseeded RNG in the arena.** None. The arena script draws randomness exactly
twice, both `core:166(28)`, which is `FFX_Rand_Stream(2)`, a seeded stream. Its created fiends are
ordinary monsters, so their spoils go down the normal battle path, which is also seeded. The one
real clock in the program is `FFX_Rand_SeedValueFromSystemClock 0x798950`, and the arena reaches
nothing that calls it. Section 9 corrects an existing note about when that clock fires.

---

## 2. The write path, end to end

```
FFX_Btl_ApplyActionToTargets 0x7892E0      per-target loop over actor indices 0..30
    sets  targetActor + 0xDD0 = 1          "this kill may be a capture"
        if  abilityRow[+0x1C] & 0x40000     a capture-capable command
        and attackerActor[+0x6C0] & 4       the Capture auto-ability is equipped
        and g_ffxBtlScriptMode == 0         not a scripted battle

FFX_Btl_OnUnitDefeated 0x78C740            the victim's death handler
    if FFX_Btl_IsEnemySlot(victimIndex) and a3 == 0:
        FFX_SaveData_MarkMonsterBit(victim[+0x0E], GetMonsterMask2())    ; bestiary "defeated"
        if victim[+0xDD0]:
            victim[+0xDD0] = FFX_SaveData_TryCapture(victimIndex, victim[+0x6D6])
        victim[+0xDD1] = victim[+0xDD0]
        ++g_ffxBtlMonstersKilledThisBattle
        FFX_SaveData_BumpCharBattleCounter(killerIndex, 1)
    else:
        victim[+0xDD0] = 0

FFX_SaveData_TryCapture 0x790B30
    species == 255                 -> msg 12300 (0x300C) "cannot be captured",  returns 0
    GetCaptureCount(species) >= 10 -> msg 12299 (0x300B) "already have ten",    returns 0
    otherwise                      -> msg 12298 (0x300A) "captured",            returns 1
                                      and AddCaptureCount(species, +1)
    then, in all three cases, sub_79CFD0(5, victimActorIndex, msgId, 27, 35)

FFX_SaveData_AddCaptureCount 0x790B90
    g_ffxMonsterCaptureCounts[species & 0xFFF] = clamp(old + delta, 0, 10)
```

The capture result is kept for two consumers:

- `sub_78DC30` at `0x78DCF8` branches on `victim + 0xDD0` when it picks the death sequence, which
  is how you get the "pulled into the weapon" animation instead of pyreflies.
- `sub_7B2DC0`, the battle AI property getter, reads `victim + 0xDD1` at `0x7B44AE`
  (`movsx eax, byte ptr [edi+0DD1h]`, which IDA labels `jumptable 007B2E14 case 342`). So a
  monster's own AI script can see that it was captured.

### The actor fields

| actor offset | type | meaning | written by |
|---|---|---|---|
| +0x0E | word | monster type id, the index space of the two bestiary masks | unit setup |
| +0x6C0 | word | third word of the 48-bit auto-ability effect mask. **bit 2 = Capture** | `FFX_Btl_LoadCharRecordIntoActor` at 0x79CA01 |
| +0x6D6 | word | **capture species index**, 255 = not capturable | `FFX_Btl_LoadUnitParams` at 0x79B8CE (monsters) and 0x79B5C1 (characters, writes 0) |
| +0xDD0 | byte | capture pending, then the capture result | `FFX_Btl_ApplyActionToTargets` 0x789521 / 0x789530 |
| +0xDD1 | byte | capture result kept for the AI getter | `FFX_Btl_OnUnitDefeated` 0x78C847 |
| +0xF80 | ptr | monster archive section 3, the stat and parameter record | `FFX_Btl_BindMonsterArchiveSections 0x783B00` |

**Why nothing in `.text` appears to write +0x6D6.** A scan of `.text` for the displacement
`+6D6h` finds one instruction, the read in `FFX_Btl_OnUnitDefeated`. The write is
`mov [ebx+196h], ax` at `0x79B8CE`, because `FFX_Btl_LoadUnitParams` does
`lea ebx, [esi+540h]` at `0x79B509` and then `sub_79F230(ebx, 0x1EC)` to zero the block. So
`actor + 0x540 .. +0x72B` is one 492-byte **unit parameter block**, and 0x540 + 0x196 = 0x6D6.
Worth remembering for anyone else hunting an actor field that looks unwritten.

A scan for the other two displacements, for completeness: `+0xDD0` has 20 referencing
instructions, of which 4 are in `FFX_Btl_OnUnitDefeated`, 2 in `FFX_Btl_ApplyActionToTargets`, 2
more in battle (`sub_78D580`, `0x78DCF8`), 2 clears in `sub_7AF4F0` / `sub_7AFAB0`, the AI
jumptable entry, and 8 in unrelated non-battle code at 0x64xxxx-0x67xxxx that happen to use the
same offset on a different structure. `+0xDD1` has 7, only two of which are battle.

---

## 3. The capture decision, verified at instruction level

Attacker versus victim is the one thing here that would quietly wreck the design if I had it
backwards, so this is the disassembly rather than the decompiler.

`FFX_Btl_ApplyActionToTargets 0x7892E0` prologue:

```
789308  movzx ebx, byte ptr [esi]      ; esi = arg0, the action record. [esi] = the ACTING unit index
78930D  mov   [ebp+var_54], ebx
789310  call  FFX_Battle_GetActor
789315  mov   edi, eax
78932F  mov   [ebp+var_58], edi        ; var_58 = the ATTACKER actor pointer
789332  call  FFX_Btl_ResolveAbilityId
78933D  mov   [ebp+var_5C], ebx        ; var_5C = the ability / command kernel row
```

per-target loop body, target index in `ebx` running 0..30:

```
78941D  call  FFX_Battle_GetActor
789422  mov   edi, eax                 ; edi = the TARGET actor pointer
...
7894FD  mov   esi, [ebp+var_58]        ; esi = the ATTACKER again
789500  test  dword ptr [ecx+1Ch], 40000h
789507  jz    short loc_78952A
789509  test  byte ptr [esi+6C0h], 4
789510  jz    short loc_78952A
789512  cmp   byte ptr g_ffxBtlScriptMode, 0
789519  jnz   short loc_78952A
789521  mov   byte ptr [edi+0DD0h], 1  ; set on the TARGET
...
789530  mov   byte ptr [edi+0DD0h], 0
789546  cmp   ebx, 1Fh                 ; loop bound 31
```

`var_58` is never reassigned inside the loop, and `edi` is not clobbered between `0x789422` and
`0x789521`. So the `+0x6C0` test is on the **attacker** and the `+0xDD0` write is on the
**target**.

### Both data bits, confirmed against the shipped tables

**The command flag.** `battle/kernel/command.bin`, 44558 bytes, one descriptor, rows 0..319,
stride 96, data at +0x14. Exactly **24** rows have bit 0x40000 in the dword at +0x1C:

```
  0 Attack        6 Delay Attack   7 Delay Buster   8 Sleep Attack    9 Silence Attack
 10 Dark Attack  11 Zombie Attack 12 Sleep Buster  13 Silence Buster 14 Dark Buster
 15 Triple Foul  16 Power Break   17 Magic Break   18 Armor Break    19 Mental Break
 20 Mug          21 Quick Hit     89 Full Break    90 Extract Power  91 Extract Mana
 92 Extract Speed  93 Extract Ability  94 Nab Gil  265 Struggle
```

`item.bin`, `monmagic1.bin` and `monmagic2.bin` have **zero** rows with the flag. So bit 0x40000
means "a physical weapon strike that may capture", which matches the in-game rule exactly, and
magic, items and monster abilities can never capture.

**The auto-ability bit.** `FFX_Btl_LoadCharRecordIntoActor 0x79C5F0` takes each equipped piece's
auto-ability ids, masks to 12 bits, looks the row up with
`FFX_KernelTable_GetRow(id, dword_112A944, 0)` at `0x79C8B4` (`dword_112A944` is the **a_ability**
table pointer, filled by `FFX_Btl_LoadKernelTable 0x781D40`), and ORs three words in:

```
79C9EB  or [esi+6BCh], ax   <- a_ability row +0x62
79C9F6  or [esi+6BEh], ax   <- a_ability row +0x64
79CA01  or [esi+6C0h], ax   <- a_ability row +0x66
```

`battle/kernel/a_ability.bin` is 21642 bytes, one descriptor, rows 0..133, stride 108, strings at
data+14472. **Exactly one** of the 134 rows sets bit 2 of the word at +0x66: **row 122**, whose
name bytes `52 70 7F 83 84 81 74` decode to **"Capture"**. (FFX's text encoding is `'A' = 0x50`,
`'a' = 0x70`, which I derived from this very string and then used to decode the command names
above.) Row 122's +0x62 and +0x64 words are both zero, so bit 34 of the 48-bit effect mask is
Capture and nothing else.

**The script-mode byte.** `g_ffxBtlScriptMode 0x112C9E5`. Only two writers in the whole binary,
both ATEL battle syscalls: `Btl_275 0x7A8090` writes 1 and `Btl_276 0x7A80A0` writes 2. Value 2
also suppresses the per-character battle statistics and the steal and drop handlers. It is
script-driven, so under lockstep it replicates like any other script effect.

### The species index is static shipped data

`FFX_Btl_BindMonsterArchiveSections 0x783B00` sets `enemyUnit + 0xF80 = archiveBase +
sectionOffset[3]` from the loaded `battle/mon/_mNNN/mNNN.bin`. `FFX_Btl_LoadUnitParams` then does
`movzx eax, word ptr [edx+78h]` at `0x79B8C7` with `edx = actor + 0xF80`.

I checked that against the shipped files by parsing the header
`{ u32 sectionCount; u32 sectionOffset[] }` and reading `section[3] + 0x78`:

| file | +0x78 | expected capture index |
|---|---|---|
| m001 | 0 | 0 |
| m002 | 1 | 1 |
| m003 | 2 | 2 |
| m005 | 4 | 4 |
| m009 | 8 | 8 |
| m022 | 21 | 21 |
| m040 | 39 | 39 |
| m084 | 68 | 68 |
| m095 | 75 | 75 |
| m181 | 76 | 76 |
| m239 | 100 | 100 |
| m100, m150, m200, m250, m360 | 255 | not in the arena list |
| m105, m276, m283, m292, m318 | 255 | arena creations, never captured |

Every one of the 11 arena-list fiends I sampled returns its exact row index in
`g_ffxArenaSpeciesTable`, every non-arena monster returns 255, and the arena creations return 255
too, which fits them being marked by script rather than captured. All the monster files I checked
have `sectionCount == 8`.

### The actors capture operates on

`FFX_Btl_IsEnemySlot 0x79AEF0` is literally `return (unsigned)(index - 20) <= 7`, and it gates the
entire capture and bestiary branch. `sub_793530` pins down the array layout:

```
793530  push 1E270h          ; 123504 = 3984 * 31
793535  call FFX_MemAlloc
79353A  mov  g_ffxBattleAllyActors, eax
79353F  add  eax, 13740h     ; 79680 = 3984 * 20
793549  mov  g_ffxBattleEnemyUnits, eax
793544  push 0DCE0h          ; 56544 = 912 * 62
79354E  call FFX_MemAlloc
793555  mov  g_ffxBattleMonsterActors, eax
```

So the eight combat enemies are **actor indices 20..27 in the 3984-byte array**, and
`g_ffxBattleEnemyUnits 0x1134468` is exactly `&allyActors[20]`. That is why offsets like +0x6D6,
+0xDD0 and +0xF80 are valid on an enemy. `g_ffxBattleMonsterActors 0x113446C` (indices 31..92, 912
bytes each) is a different, smaller array and is not where capture state lives. See section 12,
because FFX_GAME_NOTES.md reads as if combat enemies live there.

---

## 4. The counters

`g_ffxMonsterCaptureCounts`, `saveData + 0x420C` = `0x1130C9C`, `BYTE[512]`, only **0..138** used.

- **0..103** are the real capturable fiends, each clamped 0..10, and 10 is the arena requirement.
- **104..138** are the **35 arena creations**. Their monster files carry 255 at param+0x78, so you
  never capture them. The arena script writes a 1 into each one when its unlock condition is met,
  through `core:429`. So **`saveData + 0x4274 .. +0x4296` (VA 0x1130D04..0x1130D26) is the arena's
  creation roster**, 35 bytes inside the same array.

Accessors: `FFX_SaveData_GetCaptureCount 0x790AF0`, `FFX_SaveData_AddCaptureCount 0x790B90`,
`FFX_SaveData_TryCapture 0x790B30`. The array itself has exactly **three** referencing
instructions in the whole binary, all inside those first two functions. There is also a host-side
thunk at `0x772790` (`jmp FFX_SaveData_GetCaptureCount`) which IDA had named
`FFX_AtelSys_Movie_019_start`.

The complete caller set of `FFX_SaveData_GetCaptureCount`, which is what rules out a menu module:

```
00772794  FFX_AtelSys_Movie_019_start (the thunk)
00790B4B  FFX_SaveData_TryCapture
00790BA6  FFX_SaveData_AddCaptureCount          (reads to clamp)
0085895F  FFX_AtelSys_Core_428_GetCaptureCount
0085D3FF  FFX_AtelSys_Core_536_GetCaptureCount
00872AD1  FFX_Arena_CountSpeciesWithAtLeast     (mode 2)
00872B01  FFX_Arena_CountSpeciesWithAtLeast     (mode 1)
00872B32  FFX_Arena_CountSpeciesWithAtLeast     (mode 0)
```

and of `FFX_SaveData_AddCaptureCount`: `TryCapture`, plus syscalls 429, 430, 431 and 537. That is
all of it. **No menu code, no shop code, no UI code anywhere.**

### The two bestiary masks, question settled

GAME_STATE.md left open which of `+0x440C` and `+0x444C` is "seen" and which is "defeated".
Settled:

- `g_ffxMonsterSeenMask saveData+0x440C` = **encountered**. `FFX_Btl_SetupUnitRoster 0x79C110`
  marks a bit for every enemy type it spawns, before the fight starts.
- `g_ffxMonsterMask2 saveData+0x444C` = **defeated**. `FFX_Btl_OnUnitDefeated` marks the bit at
  `0x78C80C` when the thing actually dies.

Both are indexed on the **monster type id** (`actor + 0x0E`), not on the capture species index, so
two different index spaces live in the same 640-byte bucket. Anyone writing a debug "fill the
bestiary" patch has to respect that.

---

## 5. The species table

`g_ffxArenaSpeciesTable 0xC86708`, **139 rows of 8 bytes**. The row count comes from
`FFX_Arena_GetSpeciesCount 0xA5CB20`, which is literally `mov eax, 8Bh ; retn`.

| off | type | meaning |
|---|---|---|
| +0 | word | **monster / model id**, the same id as `actor + 0x0E` and the `mNNN` file |
| +2 | word | **area key**. 1000 = not in any area, otherwise 1001..1015 |
| +4 | word | **species key**. 2..14 for a real species group, 100 / 101 / 102 for creations |
| +6 | word | zero in all 139 rows |

The row index **is** the capture species index. There is exactly **one** referencing instruction in
the whole binary, `0x872AB6` inside `FFX_Arena_CountSpeciesWithAtLeast`, so the engine never does a
reverse monster-id to capture-index lookup. That mapping exists only in the monster files.

### The only query

`FFX_Arena_CountSpeciesWithAtLeast 0x872AA0`, `int (int mode, int key, int minCount)`:

```
mode 0 -> filter on row +2 (AREA),    branch at 0x872B1E
mode 1 -> filter on row +4 (SPECIES), branch at 0x872AED
mode 2 -> no filter, all 139 rows,    branch at 0x872AC9
return count of i in [0,139) where the row matches and GetCaptureCount(i) >= minCount
```

The count is looked up with the **loop index**, not with the row's +0 word. That is how I know the
counters are indexed by capture species index and not by monster id.

Three callers, all ATEL script syscalls in library 0 ("core"):

| syscall | handler | mode |
|---|---|---|
| `core:538` | `FFX_AtelSys_Core_538_CountAreaSpeciesAtLeast 0x85D780` | 0, area |
| `core:539` | `FFX_AtelSys_Core_539_CountSpeciesGroupAtLeast 0x85D8C0` | 1, species |
| `core:540` | `FFX_AtelSys_Core_540_CountAllSpeciesAtLeast 0x85DB90` | 2, everything |

Script argument order is `(key, minCount)` for 538 and 539, and `(minCount)` for 540.

### Group sizes, read off the table

| area key | members | capture indices |
|---|---|---|
| 1000 | 73 | not in any area |
| 1001 | 6 | 0 1 2 3 4 101 |
| 1002 | 3 | 5 6 7 |
| 1003 | 7 | 8 9 10 11 12 13 14 |
| 1004 | 6 | 15 16 17 18 19 20 |
| 1005 | 7 | 21 22 23 24 25 26 100 |
| 1006 | 3 | 27 28 29 |
| 1007 | 4 | 30 31 32 33 |
| 1008 | 5 | 34 35 36 37 102 |
| 1009 | 3 | 47 48 49 |
| 1010 | 5 | 50 51 52 53 54 |
| 1011 | 7 | 61 62 63 64 65 66 67 |
| **1012** | **1** | **68** (dead, see below) |
| 1013 | 3 | 76 77 78 |
| 1014 | 3 | 85 86 95 |
| 1015 | 3 | 91 92 93 |

| species key | members | capture indices |
|---|---|---|
| 2 | 4 | 8 15 27 59 |
| 3 | 8 | 0 9 22 34 47 50 62 85 |
| 4 | 7 | 1 10 17 28 31 79 83 |
| 5 | 10 | 2 3 11 18 25 32 36 65 71 94 |
| 6 | 9 | 4 13 19 33 55 57 72 73 80 |
| 7 | 7 | 5 16 23 40 51 63 91 |
| 8 | 8 | 6 24 35 52 64 76 87 89 |
| 9 | 9 | 7 26 44 48 54 66 68 92 98 |
| 10 | 6 | 12 29 41 42 53 88 |
| 11 | 12 | 14 20 37 39 45 46 49 58 60 69 84 86 |
| 12 | 4 | 21 30 38 61 |
| 13 | 10 | 43 56 70 75 77 78 81 90 93 97 |
| 14 | 10 | 67 74 82 95 96 99 100 101 102 103 |
| 100 | 8 | 104..111 |
| 101 | 14 | 112 113 114 115 116 117 118 119 122 123 126 130 133 136 |
| 102 | 13 | 120 121 124 125 127 128 129 131 132 134 135 137 138 |

The three creation groups line up with the group counts in a way that is hard to read as
coincidence: **13 species groups and 13 members in key 102**, **14 live area groups and 14 members
in key 101**, and **8 members in key 100** with no group to match, which is the "Original"
category. So key 100 = Originals, 101 = one per area, 102 = one per species. That specific naming
is **inference**, well supported but not proved by a code path.

---

## 6. The thresholds are in the script, and they confirm the table

The Monster Arena event package is
**`ffx_ps2/ffx/master/jppc/event/obj/na/nagi0700/nagi0700.ebp`**, 130,304 bytes, name string
`nagi0700`, author string `iwabuchi`, 20 actors, an 80,696-byte code section from 0x908 to
0x14440. "Nagi" is the Calm Lands, Nagi Plains. Disassembled with the project's `tools/ebp.py`.
Script offsets below are offsets inside that code section as `ebp.py dis` prints them.

The arena's progress check is one loop over `key = 2..14` then `1001..1015`, dispatched through a
`setsel` / `pushsel` chain at **0x1A04**. Each case hard-codes its group's member count into
`var18` and then runs the same body. Transcribed from case `key == 2` at **0x645**:

```
var18 = 4                                  ; this group's member count
if (key <= 14) var17 = core:539(key, 1)    ; species group
else           var17 = core:538(key, 1)    ; area group
if (var17 >= var18) {                      ; every member captured at least once
    var25 += 1                             ; groups complete, any kind
    if (key <= 14) var28 += 1 else var29 += 1
    var13[idx] |= bit                      ; scratch, recomputed every visit
}
if (key <= 14) {                           ; the 5 and 10 tiers are species-only
    var19 = core:539(key, 5);  if (var19 >= var18) var30 += 1
    var20 = core:539(key, 10); if (var20 >= var18) var31 += 1
}
```

so the three tiers are **1, 5 and 10 of each member**. Loop tail at 0x1AE7: after key 14 it jumps
`var21` to 1000, and it exits once `var21 >= 1015`.

The per-case member counts, read straight out of the bytecode, against the table row counts:

| key | script | table | | key | script | table |
|---|---|---|---|---|---|---|
| species 2 | 4 | 4 | | area 1001 | 6 | 6 |
| species 3 | 8 | 8 | | area 1002 | 3 | 3 |
| species 4 | 7 | 7 | | area 1003 | 7 | 7 |
| species 5 | 10 | 10 | | area 1004 | 6 | 6 |
| species 6 | 9 | 9 | | area 1005 | 7 | 7 |
| species 7 | 7 | 7 | | area 1006 | 3 | 3 |
| species 8 | 8 | 8 | | area 1007 | 4 | 4 |
| species 9 | 9 | 9 | | area 1008 | 5 | 5 |
| species 10 | 6 | 6 | | area 1009 | 3 | 3 |
| species 11 | 12 | 12 | | area 1010 | 5 | 5 |
| species 12 | 4 | 4 | | area 1011 | 7 | 7 |
| species 13 | 10 | 10 | | area 1013 | 3 | 3 |
| species 14 | 10 | 10 | | area 1014 | 3 | 3 |
| | | | | area 1015 | 3 | 3 |

**27 of 27 match, exactly.** That is mutual confirmation of three independent things at once: my
reading of the table layout, my reading of the syscall argument order, and the claim that the
thresholds are authored in script rather than compiled in.

**Area 1012 is not in that table because the script skips it.** The dispatch entry at 0x1AC4 sends
key 1012 to **0x17CD**, and 0x17CD is a bare `jmp` back to the loop tail. An empty case body. So
there are 27 live group tests, not 28, which is also why the loop tail's
`if (var25 >= 27) var14 = 1` at **0x1B14** is reachable at all. Area key 1012's single table row
(capture index 68, monster 84) is a dead area tag. I had this wrong on the first pass, see
section 13.

### What the accumulators gate

After the loop the script tests the accumulators against its own constants. Each test is wrapped
in an "already offered" bit so the announcement happens once. These eight are the Original
creations:

| guard bit | condition | script offset |
|---|---|---|
| 0x01 | `var28 >= 2` (2 species groups complete at 1 of each) | 0xB14D |
| 0x02 | `var29 >= 2` (2 areas complete) | 0xB6A1 |
| 0x04 | `var28 >= 6` | 0xBBF5 |
| 0x08 | `var29 >= 6` | 0xC149 |
| 0x10 | `var28 >= 13` (all species groups) | 0xC69D |
| 0x20 | `var30 >= 13` (all species groups at 5 of each) | 0xCBF1 |
| 0x40 | `core:428(45) >= 2 && core:428(46) >= 2 && core:428(59) >= 2 && core:428(60) >= 2` | 0xD199 |
| 0x80 | `var31 >= 13` (all species groups at 10 of each) **and** `core:572(768, 34)` | 0xD79F |

The 0x40 one is the odd member of the set: it does not use the loop at all, it asks for at least
**two** each of four specific capture indices, 45, 46, 59 and 60. Those four `core:428` calls are
the **only four uses of syscall 428 in the entire shipped game**, which is a nice independent check
that this gate is what 428 exists for.

Also in the tail, at **0x1B24**: a one-off message when `var25 >= 10` and key item flag 41000 is
not yet set (`core:352`, used 5 times in this script).

`core:540` (count every species at >= N, no filter) is **not** used by `nagi0700`. Its only user
is `nagi0000.ebp`, the Calm Lands field map itself.

---

## 7. Where the arena's own state lives

Three places, all inside `g_ffxSaveData`.

**1. The creation roster: `g_ffxMonsterCaptureCounts[104..138]`, `saveData + 0x4274 .. +0x4296`.**
`nagi0700.ebp` calls `core:429` (`AddCaptureCount(id, +1)`) exactly **35 times**, once each for
ids 104 through 138, one per creation. GAME_STATE.md's **Monsters** bucket, no new range.

Two oddities worth knowing about. `core:430` (delta -1) is called by **no shipped script at all**.
And `core:431` (`AddCaptureCount(id, n)`) is called twice, back to back, at script 0xE110 and
0xE119: `AddCaptureCount(59, 99)` and `AddCaptureCount(43, 99)`. The delta of 99 clamps to 10, so
that pair force-fills two capture species. It sits behind a `waitactor` on actor 7 and a world-flag
write, so it looks like an event hand-out rather than a debug path, but I did not chase which
conversation reaches it.

**2. ATEL `globalA` bytes in the save block.** ATEL variable storage class 0 resolves to
`atelCtx + 0x2C`, and `FFX_Atel_InstallContextCallbacks 0x871190` fills that in:

```
8711A3  or    byte ptr [esi], 1          ; esi = g_ffxAtelCtxArray + 0x238 * index
8711C6  call  FFX_GetSaveData_thunk
8711CB  add   eax, 1ECh
8711D0  mov   [esi+2Ch], eax
```

So **globalA offset N is `saveData + 0x1EC + N`**, and note the contexts are an array with stride
0x238 at `g_ffxAtelCtxArray 0x1325BA0`, all pointing their globalA at the same save block.

The arena's persistent "already offered" bits, from `ebp.py vars nagi0700`:

- `var11` = globalA 0xA0, `u8[4]` -> **`saveData + 0x28C`** = `0x112CD1C`
- `var12` = globalA 0xA4, `u8[1]` -> **`saveData + 0x290`** = `0x112CD20`

Five bytes, 40 bits, of which **35 are used**: the script does `storevar.ix` on 11 or 12 exactly
35 times, with bit values 1, 2, 4, 8, 16, 32, 64 and 128, one bit per creation. So those five bytes
are the arena's entire persistent "what have I already offered you" memory. They are in
GAME_STATE.md's **Progress** bucket (+0x00C0 .. +0x3D0C), along with the script's other globalA
variables, which span 0x8D to 0xA98 (`saveData + 0x279 .. +0xC84`).

**3. The world event flags.** `nagi0700` writes `g_ffxWorldFlagBits3DCC 0x113085C`
(`saveData + 0x3DCC`, 1024 bits, documented in WORLD_STATE.md) **1359 times** through `core:528`,
and reads it through `core:571` (36 uses) and `core:572` (37 uses). That makes the arena by far the
heaviest user of that array in the game. `saveData + 0x3DCC` falls in GAME_STATE.md's bucket
labelled **Config**, which is a misleading name, see section 12.

**What does not persist.** `var13`, `var14` and `var15` are ATEL class 6, which
`FFX_Atel_ResolveVarAddress 0x86C2E0` resolves into the loaded `.ebp` image itself, and
`var17..var21` and `var25`, `var28..var31` are class 3 per-actor scratch. The accumulators and the
27 per-group completion bits are all scratch: **the script recomputes the whole picture from the
capture counts every time you talk to the owner.** That is good news for co-op, because there is no
cached derived arena state that could drift out of step.

---

## 8. The arena as a screen

**There is no arena menu module.** The caller lists in section 4 are the proof: nothing in the
menu-module range, nothing in the native main-menu page system (`FFX_Menu_Step 0xA53570`), and
neither shop module reads a capture count or calls the arena query.

What the arena actually uses, from a syscall census of all of `nagi0700.ebp`:

| syscall | uses | what |
|---|---|---|
| `core:528` set world flag | 1359 | its persistent progress |
| `core:0` wait frames | 450 | |
| `core:132` / `core:124` wait for confirm | 356 / 298 | plain message boxes |
| `core:157` | 305 | |
| `core:100` / `101` / `102` / `106` / `107` | ~305 each | set message, position, palette, open, close |
| `core:143` window idle test | 293 | |
| **`core:539`** / **`core:538`** | 109 / 52 | the threshold queries |
| **`core:315`** ask a choice | 48 | `FFX_AtelOp_AskChoiceComposite 0x8607A0`, attribute forced to `attr | 2` = **list choice window** |
| **`core:347`** treasure handout | 38 | the rewards |
| **`core:429`** add capture count | 35 | the creation roster |
| `core:572` / `core:571` world flag reads | 37 / 36 | gates |
| `core:352` key item test | 5 | one message gate |
| **`core:428`** get capture count | 4 | the 0x40 Original gate |
| `core:431` add capture count by N | 2 | the force-fill pair |
| **`core:166`** scaled random | 2 | `FFX_Rand_Stream(2)`, both `core:166(28)` |

So the capture list, the creation list and every reward menu in the arena are the ordinary **ATEL
list-choice window**, and the handouts are the ordinary **treasure syscall**.

**What is per-screen rather than per-player, which is the question that was asked.** All of it,
and the relevant single slots are already named in other docs:

1. **The eight message windows share one global input focus.** `FFX_MesWin_StepAll 0x8AB910` runs
   the input handler only for the window whose `g_ffxMesWinObjects[i] + 6` is 1. The script picks
   the window, so two scripts cannot even pose two questions with two live cursors.
2. **One choice object.** `g_ffxMesWinChoiceObjects 0x1868A90`, 0x138 bytes, cursor at +2. It is
   double banked by `FFX_MesWin_SelectObjectBank 0x8AF5C0`, but as DIALOGUE.md already says the
   bank is a **mode** selector (field versus in-game menu), not a player selector.
3. **One treasure staging buffer.** `g_ffxTreasureStaging 0x2310EA0`, which GAME_STATE.md already
   documents field by field. The arena hands out 38 rewards through it, so two rewards resolving in
   the same step would clobber each other. Same hazard as a chest, no worse.
4. **One pending menu mode.** `FFX_AtelSys_Core_347_poll` pushes `0x10001` at `0x85AF9F` and calls
   `FFX_MenuSys_RequestOpen`, which stores it in `g_ffxMenuRequestedMode 0x12FBC38`. Mode 0x10000
   is module 17, the "you got X" results screen. That is the global MENU_SYNC.md already watches.
   (`core:423`, the silent chest, does the same at `0x857F24`.)

Practical consequence: **the arena needs no new menu work at all.** If the ATEL script layer, the
message-window pad and the pending menu mode are replicated, the whole arena conversation
replicates with them. What it does need is the rule that only one player may be in the arena
conversation at a time, which is the same rule the dialogue layer already needs for any NPC.

---

## 9. Clocks and randomness

**In the arena: two draws and nothing else.** Both are `core:166(28)` at script 0x1BDC and
0x10F49, i.e. pick one of 28, used to vary a line of dialogue. `FFX_AtelSys_Core_166 0x857400` is
`(FFX_Rand_Stream(2) & 0xFFFF) * n >> 16`, with `n` forced to 1 when the script passes <= 0.
Stream **2** is the ATEL script stream, and syscalls 166 and `169 0x857680` are its only two static
call sites.

**The created fiends' spoils are not special.** An arena creation is an ordinary monster, so it
takes its stats from its own `mNNN.bin` and its drops from the ordinary battle path. The rolls are
on the seeded streams:

- `sub_7990D0`, called from `FFX_Btl_OnUnitDefeated` at `0x78C952`, draws `FFX_Rand_Stream` **five**
  times.
- `sub_78B760` (which also calls `FFX_SaveData_AddItem`) and `sub_78B920`, called from
  `FFX_Btl_ApplyActionToTargets` at `0x7894B9` and `0x7894E5`, draw twice each. Both test
  `g_ffxBtlScriptMode` first, which is the suppression mentioned in section 3.

All seeded, all already in RANDOM_ENCOUNTER.md's scope. Nothing arena-specific to do.

**The one real clock.** `FFX_Rand_SeedValueFromSystemClock 0x798950` xors the eight bytes of a
`GetSystemTime` buffer. Its only caller is `FFX_Rand_SeedAllStreams 0x798890`, whose only caller is
`FFX_InitNewSaveData 0x786B00`, which is reached from `FFX_Btl_Init 0x781700`, from the battle
debug console, and from ATEL syscall `core:573 0x856520`. `nagi0700` calls `core:573` zero times.
But the existing IDB comment on `g_ffxRandStreamState` says the streams are seeded "once at boot",
and that is not right, see section 12.

**`core:572` is a boolean, not a count.** I had this noted as "count consecutive flags", and the
disassembly says otherwise:

```
85C60E  call FFX_Atel_PopInt     ; ebx = the LAST script argument = N
85C617  call FFX_Atel_PopInt     ; edi = the FIRST argument = base flag id
85C627  loop: if (!TestWorldFlag3DCC(esi + edi)) break; ++esi; while (esi < ebx)
85C642  setz al                  ; return (esi == ebx)
```

so `core:572(base, n)` is **"are all n consecutive world flags from base set"**. The arena's use,
`core:572(768, 34)`, is "all 34 flags from 768". Corrected in the IDB and the function renamed
`FFX_AtelSys_Core_572_AllWorldFlagsSet`.

---

## 10. Is any of this in a magic DLL

**No**, and I checked it in both directions.

`g_ffxMagicHostApiTable` (RVA 0x864CE8, VA 0xC64CE8) has 741 entries. I resolved all 741 and
bucketed them. 118 land in the battle module 0x780000-0x7C0000, and the shape of that set is
unmistakably an **animation, effect and camera API**: unit positions, rotations, heights, ground
modes, effect slots, `FFX_Came_OpenHandle` / `SetPos` / `GetProjMatrix`, plus `FFX_Btl_Rand` (index
324) and `FFX_Btl_RandFloat1to2` (index 307).

Of the functions in this subject, **exactly one** is in the table: `FFX_Btl_IsEnemySlot 0x79AEF0`
at index **293**. `FFX_SaveData_GetCaptureCount`, `AddCaptureCount`, `TryCapture`,
`MarkMonsterBit`, both mask getters, `FFX_Arena_CountSpeciesWithAtLeast`,
`FFX_Arena_GetSpeciesCount`, `FFX_Battle_GetActor`, `FFX_Btl_GetEnemyUnit`, `FFX_GetSaveData` and
`FFX_KernelTable_GetRow` are **all absent**. So **no `magic_NNNN.dll` can read or write a capture
count, and none can reach the save block through this table.**

The 581 DLLs in `magicFiles\FFX\` (magic_0003 to magic_0712) are one per ability effect, each
exporting `GetEffectOverlayTable` and `InitMagicPRX`. The Capture weapon's visual is very likely one
of them, which matters for presentation and not at all for state. I did not disassemble one to
confirm that, see section 13.

One residual, and it is small. A DLL does get actor pointers through the table
(`FFX_Btl_GetUnitPos` and friends), so in principle one could poke `actor + 0xDD0` behind the
engine's back between the two halves of the capture decision. Even then its inputs would be the
same replicated actor and ability on both machines, so it would do the same thing on both. Not a
divergence risk, just a "do not be surprised" note.

---

## 11. Addresses derived, as RVAs

RVA = VA - 0x400000. Names in bold are ones I created or corrected in this pass and are now in the
IDB with the evidence in a function comment.

### Functions

| RVA | name | note |
|---|---|---|
| **0x38C740** | **`FFX_Btl_OnUnitDefeated`** | was `sub_78C740`. The victim death handler and the ONLY caller of TryCapture. Capture branch 0x38C7F1..0x38C849 |
| **0x3892E0** | **`FFX_Btl_ApplyActionToTargets`** | was `sub_7892E0`. Sets the capture-pending flag. Decision at 0x389500..0x389537 |
| 0x390B30 | `FFX_SaveData_TryCapture` | `(victimActorIndex, captureSpeciesIndex) -> 0/1`, messages 12298/12299/12300 |
| 0x390B90 | `FFX_SaveData_AddCaptureCount` | `(captureSpeciesIndex, delta)`, clamps 0..10, refuses index 255 |
| 0x390AF0 | `FFX_SaveData_GetCaptureCount` | `g_ffxMonsterCaptureCounts[idx & 0xFFF]`. 8 callers, all listed in section 4 |
| 0x390BE0 | `FFX_SaveData_MarkMonsterBit` | `(monsterTypeId, maskBase)` |
| 0x390B10 | `FFX_SaveData_GetMonsterMask2` | returns the **defeated** mask, saveData+0x444C |
| 0x390B20 | `FFX_SaveData_GetMonsterSeenMask` | returns the **encountered** mask, saveData+0x440C |
| **0x472AA0** | **`FFX_Arena_CountSpeciesWithAtLeast`** | was `sub_872AA0`. `(mode, key, minCount)`. The arena's only code-side query |
| **0x65CB20** | **`FFX_Arena_GetSpeciesCount`** | was `sub_A5CB20`. `mov eax, 8Bh ; retn`, i.e. 139 |
| **0x39AEF0** | **`FFX_Btl_IsEnemySlot`** | was `sub_79AEF0`. `(index - 20) <= 7`. Host API index 293 |
| **0x39B4F0** | **`FFX_Btl_LoadUnitParams`** | was `sub_79B4F0`. `lea ebx,[esi+540h]` at 0x39B509, zeroes 0x1EC, writes actor+0x6D6 at 0x39B8CE |
| **0x383B00** | **`FFX_Btl_BindMonsterArchiveSections`** | was `sub_783B00`. Sets enemyUnit +0xF78/+0xF7C/+0xF80/+0xF84 from archive sections |
| 0x39C5F0 | `FFX_Btl_LoadCharRecordIntoActor` | ORs a_ability +0x62/+0x64/+0x66 into actor +0x6BC/+0x6BE/+0x6C0 at 0x39C9EB / 0x39C9F6 / **0x39CA01** |
| 0x38CE50 | `FFX_Btl_ResolveAbilityId` | turns an action's id words into the kernel row whose +0x1C is tested for 0x40000 |
| 0x390A50 | `FFX_Btl_GetMonsterAbilityRow` | the monster-ability row path, no row of which carries 0x40000 |
| 0x3AB870 | `FFX_KernelTable_GetRow` | the kernel .bin row lookup |
| 0x381D40 | `FFX_Btl_LoadKernelTable` | fills the a_ability table pointer at 0xD2A944 |
| 0x394020 | `FFX_Battle_GetActor` | index < 31 at 3984 stride, 31..92 at 912 |
| 0x395AA0 | `FFX_Btl_GetEnemyUnit` | `g_ffxBattleEnemyUnits + 3984 * i`, i in 0..7 |
| 0x393530 | `sub_793530` | the actor array allocator. Proves 3984 x 31, +0x13740, 912 x 62 |
| 0x39C110 | `FFX_Btl_SetupUnitRoster` | marks the **encountered** bestiary bit per spawned enemy type |
| 0x385A00 | `FFX_SaveData_BumpCharBattleCounter` | returns early for index >= 18. The only "who" on the capture path |
| 0x39CFD0 | `sub_79CFD0` | the battle message call TryCapture makes, `(5, actorIdx, msgId, 27, 35)`. Not further identified |
| 0x3B2DC0 | `sub_7B2DC0` | battle AI property getter. Reads actor+0xDD1 at **0x3B44AE**, jumptable case 342 |
| 0x38DC30 | `sub_78DC30` | death sequence selector, branches on actor+0xDD0 at **0x38DCF8** |
| 0x3990D0 | `sub_7990D0` | post-kill spoils rolls, 5 x FFX_Rand_Stream. Called from 0x38C952 |
| 0x38B760 / 0x38B920 | `sub_78B760` / `sub_78B920` | steal and drop, 2 rand draws each, both gated on g_ffxBtlScriptMode. Called from 0x3894B9 / 0x3894E5 |
| 0x4BD9C0 / 0x4BDB70 | `sub_8BD9C0` / `sub_8BDB70` | the results-screen commits (AddItem / AddEquipEntry). Neither draws RNG |
| 0x3988F0 | `FFX_Rand_Stream` | the 68 seeded streams. Stream 2 is the ATEL script stream |
| 0x398890 | `FFX_Rand_SeedAllStreams` | reseeds all 68 plus g_ffxBattleRandState. Only caller is FFX_InitNewSaveData 0x386B00 |
| 0x398950 | `FFX_Rand_SeedValueFromSystemClock` | the only wall clock on any of this |
| 0x471190 | `FFX_Atel_InstallContextCallbacks` | sets `ctx+0x2C = saveData + 0x1EC` at 0x4711CB, the globalA base |
| 0x46C2E0 | `FFX_Atel_ResolveVarAddress` | the ATEL variable storage-class switch |
| 0x4607A0 | `FFX_AtelOp_AskChoiceComposite` | the `core:315` body, forces attribute bit 2, the list choice window |
| 0x4AB910 | `FFX_MesWin_StepAll` | runs input only for the window holding the single global focus |
| 0x4AF5C0 | `FFX_MesWin_SelectObjectBank` | the field / in-game-menu bank selector, not a player selector |
| 0x653570 | `FFX_Menu_Step` | the native main-menu page system. Reads no capture state |

### ATEL script syscalls, all library 0 ("core")

| RVA | name | note |
|---|---|---|
| **0x458950** | **`FFX_AtelSys_Core_428_GetCaptureCount`** | `core:428(speciesIndex)`. Used **4 times in the whole game**, all in the 0x40 Original gate |
| **0x458B30** | **`FFX_AtelSys_Core_429_AddCapture1`** | delta +1. **The creation unlock, 35 call sites, ids 104..138** |
| **0x458DF0** | **`FFX_AtelSys_Core_430_SubCapture1`** | delta -1. Called by no shipped script |
| **0x458E70** | **`FFX_AtelSys_Core_431_AddCaptureN`** | `(speciesIndex, delta)`. 2 uses, both force-fills with delta 99 |
| **0x45D3F0** | **`FFX_AtelSys_Core_536_GetCaptureCount`** | the duplicate of 428 that scripts actually use |
| **0x45D500** | **`FFX_AtelSys_Core_537_AddCaptureN`** | the duplicate of 431 |
| **0x45D780** | **`FFX_AtelSys_Core_538_CountAreaSpeciesAtLeast`** | `(areaKey, minCount)`, mode 0. 52 uses |
| **0x45D8C0** | **`FFX_AtelSys_Core_539_CountSpeciesGroupAtLeast`** | `(speciesKey, minCount)`, mode 1. 109 uses |
| **0x45DB90** | **`FFX_AtelSys_Core_540_CountAllSpeciesAtLeast`** | `(minCount)`, mode 2. Used by nagi0000, not nagi0700 |
| 0x45C3B0 | `FFX_AtelSys_Core_528_resi` | `SetWorldFlag3DCC(id, 1)`. 1359 arena uses |
| 0x45C490 | `FFX_AtelSys_Core_571_resi` | `TestWorldFlag3DCC(id)`. 36 arena uses |
| **0x45C600** | **`FFX_AtelSys_Core_572_AllWorldFlagsSet`** | was `..._572_resi`. `(base, n) -> all n flags set`. Loop 0x45C627, `setz` 0x45C642 |
| 0x45B900 | `FFX_AtelSys_Core_352_resi` | `TestKeyItemFlag(id)`. Arena uses it with 41000 |
| 0x457400 | `FFX_AtelSys_Core_166_resi` | `(FFX_Rand_Stream(2) & 0xFFFF) * n >> 16`. The arena's only RNG, twice, both n = 28 |
| 0x457680 | `FFX_AtelSys_Core_169_resi` | raw `FFX_Rand_Stream(2) & 0xFFFF`. Unused by the arena |
| 0x45A8A0 | `FFX_AtelSys_Core_347_start` | the treasure handout, 38 arena uses. Its poll pushes 0x10001 at **0x45AF9F** |
| 0x457B70 | `FFX_AtelSys_Core_423_start` | the silent chest. Same 0x10001 request at **0x457F24** |
| 0x4602A0 | `FFX_AtelOp_AskChoice315_start` | ask-a-choice, 48 arena uses. Body is 0x4607A0 |
| 0x456520 | `FFX_AtelSys_Core_573_resi` | new game. Calls FFX_InitNewSaveData, so a script CAN reseed the RNG from the clock |
| 0x3A8090 / 0x3A80A0 | `FFX_AtelSys_Btl_275_resi` / `276_resi` | write `g_ffxBtlScriptMode` 1 and 2. The only writers |
| 0x372790 | thunk | `jmp FFX_SaveData_GetCaptureCount`. IDA has it as `FFX_AtelSys_Movie_019_start` |

### Data

| RVA | name | note |
|---|---|---|
| **0x886708** | **`g_ffxArenaSpeciesTable`** | was `unk_C86708`. 139 x 8 bytes. +0 monster id, +2 area key, +4 species key, +6 zero. Exactly one xref in the binary |
| 0xD30C9C | `g_ffxMonsterCaptureCounts` | saveData+0x420C, BYTE[512], 0..138 used. **Indexed by capture species index, not monster id**. Exactly 3 xrefs |
| 0xD30D04 .. 0xD30D26 | (no symbol) | saveData+0x4274..+0x4296, counts[104..138], the 35 arena creation markers |
| 0xD30E9C | `g_ffxMonsterSeenMask` | saveData+0x440C, 512 bits, **encountered**, indexed by monster type id |
| 0xD30EDC | `g_ffxMonsterMask2` | saveData+0x444C, 512 bits, **defeated**, indexed by monster type id |
| **0xD2C9E5** | **`g_ffxBtlScriptMode`** | was `unk_112C9E5`. Must be 0 for a capture to be flagged |
| **0xD2C9EB** | **`g_ffxBtlMonstersKilledThisBattle`** | was `unk_112C9EB`. BYTE, incremented per monster death |
| **0xD34468** | **`g_ffxBattleEnemyUnits`** | was `dword_1134468`. Exactly `g_ffxBattleAllyActors + 0x13740` = `&allyActors[20]` |
| 0xD334D4 | `g_ffxBattleAllyActors` | 3984 x 31 = 0x1E270 bytes. Enemies are indices 20..27 here |
| 0xD3446C | `g_ffxBattleMonsterActors` | 912 x 62 = 0xDCE0 bytes, indices 31..92. **Not** where the eight combat enemies live |
| 0xD2A944 | `dword_112A944` | the a_ability kernel table pointer, used at 0x39C8A8 |
| 0xD3085C | `g_ffxWorldFlagBits3DCC` | saveData+0x3DCC, 1024 bits. The arena writes it 1359 times |
| 0xD2CD1C | (no symbol) | saveData+0x28C, 4 bytes. ATEL globalA 0xA0. 32 of the 35 creation "already offered" bits |
| 0xD2CD20 | (no symbol) | saveData+0x290, 1 byte. ATEL globalA 0xA4. The remaining 3, plus the 8 Original guard bits |
| 0xF25BA0 | `g_ffxAtelCtxArray` | ATEL contexts, stride 0x238. `+0x2C` = globalA base = `saveData + 0x1EC` |
| 0x1465B3C | `g_ffxMesWinObjects` | 8 window shells. `+6 == 1` is the single global input focus |
| 0x1468A90 | `g_ffxMesWinChoiceObjects` | the 0x138-byte choice object, live cursor at +2 |
| 0x1F10EA0 | `g_ffxTreasureStaging` | the single reward decode buffer. 38 arena rewards pass through it |
| 0xEFBC38 | `g_ffxMenuRequestedMode` | where `FFX_MenuSys_RequestOpen(0x10001)` lands |
| 0x864CE8 | `g_ffxMagicHostApiTable` | 741 entries. None of the capture or save-data accessors is in it |

### Offsets, not addresses

| what | offset | note |
|---|---|---|
| battle actor | +0x540, size 0x1EC | the unit parameter block, zeroed by `FFX_Btl_LoadUnitParams` |
| battle actor | +0x6C0 bit 2 | **has the Capture auto-ability**. Block offset 0x180 |
| battle actor | +0x6D6 word | **capture species index**, 255 = no. Block offset 0x196 |
| battle actor | +0xDD0 byte | capture pending, then the result |
| battle actor | +0xDD1 byte | capture result for the AI getter, case 342 |
| battle actor | +0xF80 ptr | monster archive section 3, the parameter record |
| battle actor | +0x0E word | monster type id, the bestiary index space |
| monster `mNNN.bin` | header | `{ u32 sectionCount; u32 sectionOffset[] }`, sectionCount 8 in every file checked |
| monster param record | +0x78 word | **the capture species index** |
| `command.bin` row | +0x1C bit 0x40000 | capture-capable command. 24 of 320 rows |
| `command.bin` | rows 0..319, stride 96, data +0x14 | |
| `a_ability.bin` row | +0x66 bit 2 | the Capture auto-ability. **Row 122**, name "Capture" |
| `a_ability.bin` | rows 0..133, stride 108, data +0x14, strings +14472 | |
| FFX text encoding | `'A' = 0x50`, `'a' = 0x70` | derived from the "Capture" string, then used for the command names |
| save block | +0x420C + 104..138 | the 35 arena creation markers |
| save block | +0x28C .. +0x290 | the 35 "already offered" bits, 5 bytes |
| save block | +0x1EC | the ATEL globalA base |

### Game data files

```
ffx_ps2/ffx/master/jppc/event/obj/na/nagi0700/nagi0700.ebp   THE MONSTER ARENA SCRIPT
ffx_ps2/ffx/master/jppc/event/obj/na/nagi0000/nagi0000.ebp   Calm Lands field, the only user of core:540
ffx_ps2/ffx/master/jppc/battle/mon/_mNNN/mNNN.bin            per-monster data, param +0x78
ffx_ps2/ffx/master/new_uspc/battle/kernel/command.bin        the 0x40000 flag
ffx_ps2/ffx/master/new_uspc/battle/kernel/a_ability.bin      the Capture auto-ability, row 122
```

### Script offsets inside nagi0700.ebp

```
0x0645  the shared group-test body (case key == 2)
0x1A04  the 27-case setsel / pushsel dispatch chain
0x17CD  area key 1012's case body, a bare jmp. The dead area
0x1AE7  loop tail, key 14 -> 1001, exit at >= 1015
0x1B14  if (var25 >= 27) var14 = 1
0x1B24  the "var25 >= 10 and key item 41000 unset" one-off message
0xB14D 0xB6A1 0xBBF5 0xC149 0xC69D 0xCBF1 0xD199 0xD79F   the eight Original creation gates
0xE110 0xE119   core:431 force-fills, AddCaptureCount(59, 99) and (43, 99)
0x1BDC 0x10F49  the two core:166(28) RNG draws
```

---

## 12. Errors and gaps in the existing notes

1. **GAME_STATE.md, "Monster capture and the bestiary":** `g_ffxMonsterCaptureCounts` is described
   as **indexed by `(monsterId & 0xFFF)`**. That is wrong. It is indexed by a **capture species
   index 0..138**. `FFX_Arena_CountSpeciesWithAtLeast` passes the loop index 0..138 straight into
   `FFX_SaveData_GetCaptureCount`, and `FFX_Btl_OnUnitDefeated` passes `actor + 0x6D6`, which is
   `monsterParam + 0x78` out of the monster's own shipped file. Verified against 21 monster files.
   The `& 0xFFF` is just defensive masking. This matters: a tool or a patch that writes "set this
   fiend's capture count" by monster id writes the wrong byte for nearly every fiend.
2. **GAME_STATE.md, same section, contradicts itself.** It says of +0x440C "so this is the
   bestiary 'encountered' list", and then two lines later "Which of the two is 'seen' and which is
   'defeated' I did not establish." The first sentence is right and the second can go. +0x440C is
   **encountered** (`FFX_Btl_SetupUnitRoster` marks it before the fight) and +0x444C is
   **defeated** (`FFX_Btl_OnUnitDefeated` marks it on death, at 0x78C80C).
3. **GAME_STATE.md bucket table:** the bucket named **"Config", +0x3D0C .. +0x3ECC, 448 bytes**, is
   mostly not config. By GAME_STATE.md's own layout section the real `conf` block is +0x3D48 and
   0x84 bytes. **256 of the bucket's 448 bytes are the two 1024-bit world event flag arrays** at
   +0x3DCC and +0x3E4C that WORLD_STATE.md documents, and they are the most script-written region
   of the whole save block. One event package alone writes the first of them 1359 times. The range
   is right, the label will mislead whoever reads a desync report.
4. **The IDB data comment on `g_ffxRandStreamState 0x1135EE0`** says the streams are seeded
   "from the wall clock, once at boot". That parenthetical is too strong, and it contradicts the
   much better function comment sitting on `FFX_Rand_SeedAllStreams 0x798890` a few addresses away,
   which correctly names all three paths into `FFX_InitNewSaveData 0x786B00`: `FFX_Btl_Init
   0x781700` (the retail once-per-launch path), `SG_DebugWin_BattleConsoleProc`, and ATEL syscall
   `core:573 0x856520`. The third one means a script **can** reseed all 68 streams plus
   `g_ffxBattleRandState` from the system clock mid-session, so anything that trusts the short
   comment and snapshots the streams only at session start is making an assumption. I appended a
   correction to the function comment rather than editing anyone's original text.
5. **RANDOM_ENCOUNTER.md's `FFX_Rand_Stream` call-site table** lists streams 0, 1 and 18 and omits
   **stream 2**, which is the ATEL script stream with two static sites, `0x85741D` (`core:166`) and
   `0x857682` (`core:169`). An addition rather than an error, but it is the stream most likely to
   be drawn from while a player is standing in a conversation.
6. **FFX_GAME_NOTES.md's** line "`0x113446C` `g_ffxBattleMonsterActors` base pointer, slots 31..92 x
   912 bytes" reads as if combat enemies live there. They do not. The eight combat enemies are
   **actor indices 20..27 in the 3984-byte ally array**, which is what `FFX_Btl_IsEnemySlot` tests
   and what `sub_793530` allocates. Offsets like +0x6D6 and +0xF80 would be out of bounds in a
   912-byte record. What indices 31..92 are actually for, I did not establish.
7. Minor: the thunk at `0x772790` carries IDA's name `FFX_AtelSys_Movie_019_start`, which is
   misleading for a `jmp FFX_SaveData_GetCaptureCount`. I left the name alone in case it is a
   genuine Movie-library slot and commented it instead.

---

## 13. What is untested, and what I think is most likely wrong, ranked

**1. "Capture replicates for free" has not been run.** This is the headline claim and it is a
static argument, not an experiment. The argument is strong, and I checked the attacker and victim
registers in the disassembly on purpose, but the honest test is two machines, same inputs, a
Capture weapon, one fiend killed, then compare the 640-byte Monsters bucket.
**The most likely failure mode is not the capture, it is the message.** `FFX_SaveData_TryCapture`
calls `sub_79CFD0(5, victim, msgId, 27, 35)` on every path, so every capture attempt pops a battle
message window, and the message layer has a single global focus. If the battle message queue ever
runs at a different depth on the two machines, the count still records identically but the
presentation drifts.

**2. I was already wrong once about area key 1012, which should lower your confidence in my script
reading generally.** My first pass had it sharing area 1013's threshold of 3 and therefore being
permanently incomplete. The bytecode says the dispatch target 0x17CD is a bare `jmp`, so the case
is simply empty. I only caught it by re-reading with real offsets. The corrected version is
self-consistent (27 live groups, 27 of 27 counts matching, and the `var25 >= 27` test reachable),
but the same class of mistake could be hiding in the gate table in section 6, which I transcribed
the same way.

**3. The names behind species keys 100 / 101 / 102 are inference.** The counts line up perfectly
(8 Originals, 14 live areas to 14 members, 13 species groups to 13 members), but no code path says
"Original" or "Area Conquest". If a UI depends on it, read the arena's own message table rather
than trusting me.

**4. The `core:431` force-fill pair at script 0xE110 is unexplained.** It sets capture species 59
and 43 to 10 outright. I know what it does, not when it runs. If it is reachable from a
conversation, a co-op session where only one player triggers it would still replicate (it is
script), but someone should find out what it is for, because a save-state write of that shape
deserves a name.

**5. Whether the Capture visual is a magic DLL.** I proved no DLL can touch the capture counts,
which is the part that matters, and I did not prove where the "pulled into the weapon" animation
lives. A byte-pattern scan of the 581 DLLs for the actor displacements was worthless because
0x00000DD0 and 0x000006C0 are too common as ordinary data. `sub_78DC30` at 0x78DCF8 is where to
start if anyone needs to know.

**6. The exact window index the arena's lists use.** I established that the arena goes through the
shared 8-slot message window system and that only one window can hold input focus. I did not
enumerate which slot each arena screen asks for, so I cannot say whether two arena screens would
collide on the same slot or merely on the focus.

**7. `g_ffxBattleMonsterActors` (indices 31..92, 912 bytes).** I showed the capture path does not
use it. I do not know what it is for, and if it turns out to hold a second copy of anything
capture-related, this is where that would bite.

**8. The three-player case specifically.** Everything here is one global collection and
single-slot screens, so a third player changes nothing structurally. But I have not thought about
whether three captures landing in the same simulation step can interleave inside
`FFX_SaveData_AddCaptureCount`. They cannot in a single-threaded lockstep step, which is why I am
not worried, but that is an assumption about the mod's architecture rather than something I read
out of the binary.
