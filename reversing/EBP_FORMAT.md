# The `.ebp` event script format

397 event packages in `FFX_Data.vbf`, holding the ATEL script bytecode that drives every cutscene,
NPC and trigger in the game. Disassembler: `../tools/ebp.py`, which parses and structurally verifies
**397 of 397**.

Loader: **`FFX_Ev_LoadEventPackage 0x872EF0`**. The reason the dword `0x31305645` appears nowhere in
`.text` is that the loader only compares two bytes:

```c
if (*(BYTE*)base != 'E' || base[1] != 'V')
    while (1) ;              // hard hang, there is no error path
```

The package is fetched by resource class 12 via `sub_88D2C0(12, 18 * gameModeId)`.

## Where they live

```
ffx_ps2/ffx/master/jppc/event/obj/<xx>/<name>/<name>.ebp
ffx_ps2/ffx/master/jppc/event/obj/<xx>/<name>/<name><NN>.mgrp     motion groups, see MGRP_FORMAT.md
```

**All 397 are under `jppc` only.** There is no per-language `.ebp` tree. Localisation is separate:
`ffx_ps2/ffx/master/<new_XXpc>/event/obj_ps3/<xx>/<name>.bin` plus `.ftc`, loaded by
`FFX_LoadLocalizedBin 0x88CAC0` and `FFX_LoadLocalizedFtc`.

## Container

| off | size | meaning | status |
|---|---|---|---|
| `+0x00` | 4 | `"EV01"`, only `'E'` and `'V'` are checked | confirmed |
| `+0x04` | 4n | section offsets from file start, **`-1` terminated**. The last entry before the terminator is an end-of-data sentinel, not a section | confirmed |
| rest | | zero in all 397 | confirmed |

It is **not** a fixed 6-slot table. 365 files have 6 entries (5 sections), 32 legacy or debug files
have 5 (4 sections). The loader reads fixed slots, so the roles are positional:

| slot | hdr off | role | evidence |
|---|---|---|---|
| 0 | `+0x04` | **ATEL script block**, always at `0x40` | `v14 = base + hdr[1]`, feeds every `FFX_Atel_*` accessor |
| 1 | `+0x08` | built-in **Japanese** message table | the fallback when `FFX_LoadLocalizedBin` returns null. 9 files are byte-identical to `new_jppc/.../<n>.bin`, the rest are an older revision of the same records |
| 2 | `+0x0C` | **SeSep** sound effect package | `FFX_Atel_StartEventSe 0x866430`. The tag `"SeSep   "` sits at `+0x20` in 348 of the 350 files that have one |
| 3 | `+0x10` | **font** block. `"FTCX"` in 324 files, 6 legacy files carry an older untagged `{u32 glyphCount, width bytes}`. **Never read on PC** | the loader never dereferences `hdr+0x10` |
| 4 | `+0x14` | built-in **US/default** message table | 81 of 294 comparable files are byte-identical to `new_uspc/.../<n>.bin`, same codec and record layout in the rest |
| last | | end of data, `== file size` in all 397 | confirmed |

The end-of-data entry matching the file size in every single file is what makes the `-1` terminated
interpretation safe rather than a guess.

## The ATEL script block

Offsets are from `file+0x40`. All confirmed unless marked.

| off | type | meaning |
|---|---|---|
| `+0x00` | u32 | **unknown**, nonzero in every file |
| `+0x04` | u32 | map entry / warp table, 32 bytes per record. The `s16` at `+0` is the map number (`FFX_Atel_GetEntryMapNo 0x86BD90`) |
| `+0x08` | u32 | short name string, author or group names like `"Tori"`, `"nojima"` |
| `+0x0C` | u32 | event name string, e.g. `"azit0000"` |
| `+0x10` | u32 | ATEL block size. Equals the end of the last label table padded to 16, with a gap of 0, 4, 8 or 12 in all 397 |
| `+0x14` | u16 | count of 1464-byte actors |
| `+0x16` | u16 | count of 1368-byte actors |
| `+0x18` | u16 | **unknown** count |
| `+0x1A` | u16 | **unknown** |
| `+0x1C` | u16 | count of 744-byte actors |
| `+0x1E` | u16 | flags. `0x8010` in 347 files, `0` in 50 |
| `+0x20` | u32 | base for variable storage class 6 |
| `+0x24` | u32 | **unknown** small block |
| `+0x28` | u32 | 16 x u16, **unknown** |
| `+0x2C` | u32 | RES, the resource and boot sub-header |
| `+0x30` | u32 | **code section.** Every entry and jump offset is relative to this |
| `+0x34` | u16 | total actor count |
| `+0x36` | u16 | "large" actor count. Actors in `[this, total)` use the 48-byte pool stride |
| `+0x38` | u32[] | one offset per actor definition record |

File order inside the block: header, actor offset array, actor records, map entry table, name strings,
variable table, int pool, float pool, **code**, resource list, RES, small tables, then the per-actor
entry and label tables, ending at `+0x10`.

### Actor definition record, 52 bytes

| off | meaning |
|---|---|
| `+0x00` | type byte: **0 subroutine**, 1 character, 2 line trigger, 3 box trigger, 4 no motion state, 5 and 6 unidentified |
| `+0x08` | u16 count of script entry points |
| `+0x0A` | u16 count of jump labels |
| `+0x14` | variable descriptor table |
| `+0x18` | int constant pool (`pushint`) |
| `+0x1C` | float constant pool (`pushflt`) |
| `+0x20` | entry point table, u32 code offsets. `callactor` runs entry 0, and the end-of-script handler resumes at entry 1 |
| `+0x24` | jump label table, u32 code offsets |
| `+0x28` / `+0x2C` / `+0x30` | bases for variable storage classes 2, 3, 4 |

**Type 0 is the subroutine container.** All 5,560 `callactor` instructions in the archive target one,
all 441 `ret` instructions sit inside one, and there are exactly 441 type-0 actors, one `ret` each.
That three-way agreement is what pins the type.

A nice confirmation of the entry-1 convention: `entry_0_1` of `azit0000` is literally
`syscall core:95` (`WaitForever`), whose poll handler returns 0 forever so the PC never advances. It
is a parked thread.

### Variable descriptor, 8 bytes

From `FFX_Atel_ResolveVarAddress 0x86C2E0`. `u32 desc` packs `[31:28]` type (0 u8, 1 s8, 2 u16, 3 s16,
4 u32, 5 s32, 6 f32), `[27:25]` storage class and `[24:0]` offset, then `u16 elementCount` and a
`u16` unknown.

Storage classes: 0 `*(ctx+0x2C)+off`, 1 `*(ctx+0x30)+off`, 2 `atel+off+actorDef[0x28]`, 3 a ctx
callback else `actorDef[0x2C]`, 4 `actorDef[0x30]`, 5 `actor+0x48+off` (the register block), 6
`atel+off+atel[0x20]`.

### Message table record, 8 bytes

From `FFX_Atel_GetMessageText 0x86BF30`: `u16 stringOffset`, `u16 attribute`, then a duplicate of both.
The pairs are identical in 40,125 of the 40,376 records. The block's first `u16` is the record array
byte size, so the count is that over 8.

## The bytecode

Encoding, from `FFX_Atel_FetchInstruction 0x869D60`:

```
b0 = code[pc]
b0 & 0x80  ->  opcode = b0 & 0x7F,  operand = code[pc+1] | code[pc+2] << 8,  length 3
otherwise  ->  opcode = b0,         no operand,                             length 1
```

**Nothing about this is guessed.** A linear sweep with that rule over all 397 files lands exactly on
the computed code-section end in every file, decoding **17,166,304 instructions**, and all
**1,478,354** entry point and label offsets land on an instruction boundary. Separately, all
**1,877,119** jump operands index inside the owning actor's label table.

### Opcodes

Cases `0x00` to `0x7A` of the 123-case switch in `FFX_Atel_RunScript 0x8641E0`. Mnemonics are ours.
`a` is the operand pushed first, `b` the one on top. Counts are real uses across all 397 files, which
is useful for telling live opcodes from dead ones.

| op | mnemonic | semantics | uses |
|---|---|---|---|
| 00, 1D, 1E, 76 | `nop` | | 12,270 |
| 01, 02 | `lor`, `land` | logical | 894 / 65,412 |
| 03, 04, 05 | `or`, `xor`, `and` | bitwise | 17,201 / 2 / 38,432 |
| 06, 07 | `eq`, `ne` | int or float by stack tag | 655,314 / 12,142 |
| 08, 0A | `gt` | identical handlers, only `0x0A` is emitted | 166,156 |
| 09, 0B | `lt` | | 62,349 |
| 0C, 0E | `ge` | | 57,414 |
| 0D, 0F | `le` | | 34,189 |
| 10, 11 | `btst`, `btstz` | `(a >> b) & 1` and its negation | 0 |
| 12, 13 | `shl`, `shr` | | 5,772 / 32 |
| 14-18 | `add sub mul div mod` | first four are int or float by tag | 1,150,140 / 42,265 / 104,936 / 22,313 / 24,645 |
| 19, 1A, 1C | `lnot`, `fneg`, `bnot` | | 8,175 / 0 / 5 |
| 1F | `pushvar` | push `var[operand][0]` | 1,220,413 |
| 20, 21 | `storevar`, `storevar.sat` | | 531,357 / 8 |
| 22 | `pushvar.ix` | pop index, push element | 742,306 |
| 23, 24 | `storevar.ix`, `.sat` | | 140,123 / 0 |
| 25 | `popr` | pop into channel R. **All 106,583 uses directly follow a `runscriptN`**, so it takes that call's result | 106,583 |
| 26 | `pushr` | push channel R | 3,348 |
| 27 | `pushvarref` | push `desc + index*elemSize`, a reference | 1,955 |
| 28, 2A | `pushcond`, `popcond` | channel `+0x20` | 0 / 115 |
| 29 | `pushsel` | push selector `ch+0x24`. 560,102 of 618,561 uses are followed by `eq` | 618,561 |
| 2B | `dup` | copy top as int. **All 12,220 uses are followed by `pushvar.ix`** | 12,220 |
| 2C | `setsel` | pop into selector. **All 75,364 uses are followed by `jmp`**, so this is a switch head | 75,364 |
| 2D, 2E, 2F | `pushint`, `pushimm`, `pushflt` | int pool / `(s16)operand` / float pool | 581,046 / 5,843,842 / 418,497 |
| 30 | `jmp` | `label[operand]` | 885,284 |
| 31, 56 | `jnz` | | 0 / 592,008 |
| 32, 57 | `jz` | | 0 / 399,827 |
| 55 | `jmp.p` | pop cond then jump unconditionally | 0 |
| 33 | `callactor` | entry 0 of actor `operand`, pushes a return frame (depth 3) | 5,560 |
| 34 | `ret` | | 441 |
| 35 | `syscallf` | syscall, result left on the stack | 198,994 |
| 58 | `syscall` | syscall, result popped into channel R | 2,068,204 |
| 36, 45-49 | `runscript0` | start another actor's script, 0 args | 79,104 |
| 37, 4A-4E | `runscript1` | 1 arg | 2,890 |
| 38, 4F-53 | `runscript2` | 2 args | 24,586 |
| 39, 3A, 3B | `runparty0/1/2` | target via `FFX_Atel_GetPartyMemberActor` | 3 / 0 / 0 |
| 3C-3F | `endscript` variants | `maybe_FFX_Atel_EndScript 0x8691D0` | 88,134 / 26 / 0 / 6 |
| 40 | `brk` | debugger breakpoint, 4 x 16-byte records at `ctx+0x64` | 20 |
| 54 | `yield` | sets flag `0x1000` then ends the script | 7,774 |
| 59-5C | `popi0..3` | pop into int regs `actor+0x48..0x54` | 10,144 total |
| 5D-66 | `popf0..9` | pop into float regs `actor+0x58..0x7C` | 88 total |
| 67-6A | `pushi0..3` | | 12,555 total |
| 6B-74 | `pushf0..9` | | 65 total |
| 75 | `pushsys` | push `dword_C52704[operand]` | 0 |
| 77, 78 | `waitactor`, `waitparty` | block on another actor's script | 13,191 / 0 |
| 79, 7A | `op79`, `op7A` | `sub_870D30(3 ints)`, `sub_86FAB0(2 ints)` | 1,566 / 0 |
| 1B, 41-44 | - | inside the `0..0x7A` range but **have no case**, so they fall to the error default | 0 |

The `uses` column is worth reading as a guide to what an assembler would actually need. Roughly 20
opcodes carry almost all real code, and about 15 are implemented but never emitted.

### System calls

A `syscall` operand is a 24-bit id: `[23:12]` library, `[11:0]` function. The libraries are listed in
the ATEL section of `FFX_GAME_NOTES.md`. `FFX_Atel_Init 0x86D6D0` registers 16 slots, 11 real and 5
pointing at a shared stub.

There is **no command name table left in the binary**, so names come one at a time from format strings
and call targets. The ones pinned so far: `core:0` WaitFrames, `core:1` SetActorModel, `core:21`
SetMoveTargetXYZ, `core:24` StartMove, `core:25` SetRotateCmd, `core:51` GetActorId, `core:93/94`
PlayerControlOn/Off, `core:95` WaitForever, `core:100/106/107` message window set/show/close,
`core:114` SetMoveFrames, `core:149` SetActorYaw, `core:152` SetMoveSpline, `core:247`
ResolvePartyMemberActor, `sg:29` sgMenu, `ch:12` chEnGravity, `ch:16` chReadSystemMGRP, `ch:23`
chSetGravMode, `ch:31` chIsMotionActive, `ch:37` chSetMotionSlot, `ch:40` chSetRunThreshold, `ch:92`
chSetNextHokan, `ch:93` chGetMotion, `ch:94` chGetMoveSpeed, `ch:101/102` chReadMotionGroupStart/Sync,
`came:59` MsCameraSetScrDpt, `came:78/79/117/120` CameScreen*, `btl:19` SetUnitMoveSpeed, `mapfx:10`
mpfpbindpos, `save:11/12` yiLoad*ParticleNo, `test:0` atel_test_print, `abmap:0` abiritymap_debug.

The full map lives in `SYSCALL_NAMES` in `../tools/ebp.py`, which is the place to add to.

## Using the disassembler

```
cd tools
python ebp.py list
python ebp.py info azit0000
python ebp.py actors azit0000
python ebp.py vars azit0000
python ebp.py text azit0000
python ebp.py dis azit0000 [-a N]
python ebp.py verify
```

It imports `vbf` so it reads straight out of the archive, and works as a library via
`ebp.EbpFile(bytes)`. Sample output:

```
entry_0_18:
  0002ED  af0800    pushflt        8   ; 34
  0002F0  d86c00    syscall        core:108
  000305  af1600    pushflt        22   ; 190.109
  000308  af0a00    pushflt        10   ; 39.986
  00030B  af1700    pushflt        23   ; 490.994
  00030E  d81500    syscall        core:21 (FFX_AtelOp_SetMoveTargetXYZ)
  00032C  d81800    syscall        core:24 (FFX_AtelOp_StartMove)
  00034A  3c        endscript
```

## Known unknowns

- **`atel+0x00`**, nonzero in every file and nothing in the loader touches it. Cross-referencing the
  ATEL block base (`ctx+0x48`) through the rest of `0x86xxxx` would find the reader.
- **`atel+0x18`, `+0x1A`, `+0x1E`.** `+0x1E` is a two-valued flags word (`0x8010` or `0`). Diffing the
  50 files with 0 against the 347 with `0x8010` for a shared trait would settle it, or a breakpoint on
  reads of `atel+0x18..0x1F`.
- **`atel+0x24` and `+0x28`** (`+0x28` is 16 u16s, which smells like a per-screen or per-channel
  default). Needs a reader search.
- **`actorDef+0x01..0x07` and `+0x0C..0x13`.** `+0x04` and `+0x06` are constant across actors within a
  file (`0x3C` / `0x82` in `azit0000`), so they are probably per-file sizes.
- **RES fields** other than `+0x04/08/0C/1C/20/2C/30/34/38/3C`. `RES+0x14` is a float (5.498), so RES
  is a mixed struct, not an offset array.
- **Message record `+0x02`**, the attribute, whose values are multiples of `0x80`, and the 251 records
  where the duplicate pair differs. Decompiling `maybe_FFX_AtelOp_MesWinShow 0x858B50`'s use of
  `window+22` would settle it.
- **The text codec above `0x89`.** Only the ranges verified by diffing the embedded US table against
  `new_uspc` are decoded: `0x3A` space, `0x3B` `.`, `0x41` apostrophe, `0x4F` `?`, `0x50-0x69` A-Z,
  `0x70-0x89` a-z. Everything else prints as `{XX}`. Aligning a file containing digits and punctuation,
  or finding the glyph order table in the FTCX block, would complete it.
- **Opcodes `0x79` and `0x7A`.** `0x79` is used 1,566 times, so `sub_870D30` and `sub_86FAB0` are worth
  decompiling.
- **Exact table sizes for libraries 7 and 11.** Usage bounds them below (235, 145) and the next `.data`
  object bounds them above (311, 383).
- **Which `.ebp` the game loads for a given map.** `sub_88D2C0(12, 18*gameModeId)` indexes a resource
  table by mode id. Dumping that table gives the map-id to event-name mapping, which is what the mod
  needs in order to know which script owns a given field.
