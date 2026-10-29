# The .data allocation map

Written 2026-10-28 for Project Zanarkand item 1.1.8: every named global in FFX.exe's `.data` with its
owning subsystem and size, and the unnamed gaps between them. Read from a private copy of the FFX.exe
IDB through idalib. Addresses are VAs at the preferred base 0x400000.

Tags: **[V]** read in the binary at the address given, **[M]** measured by a scan, **[I]** inferred. A
size taken from the distance to the next referenced address is [I] unless a reader's loop bound or an
allocation size confirms it.

Companion files:

- `DATA_MAP_TABLE.md`, the per-global table (1,300 rows), generated. Do not edit it by hand.
- `../tools/datamap.py`, which dumps the IDB and prints the coverage report and the table.
- `../tools/apply_datamap_names.py`, the 25 names this pass gives in the IDB.
- `MEMORY_ARENAS.md` for the allocators, `SAVE_BLOCK_MAP.md` for the save block byte by byte,
  `THREADS_AND_CLOCKS.md` for the thread roots, `EFFECT_OVERLAY.md` 2.5 for the menu arena.

---

## 1. The answer in short

1. **`.data` is 0xC0A000..0x25D7000, 27,054,080 bytes, and only the first 0x81600 of it is in the
   file.** Initialized data ends at **0xC8B600**. Everything from there to 0x25D7000 (26.5 MB) is
   zero fill. IDA shows 0xFF for those bytes, at runtime they read 0. [M, PE header]
2. **The done-when holds: no unexplained run over 64 KB.** 79.6% of the bytes are named, 19.7% are
   explained without a name of their own, 0.7% (199,216 bytes) are unexplained, and the largest
   unexplained run is 0x401C bytes at 0x22FD6C4. [M, `datamap.py report`]
3. **Half the section is two things.** An 8 MB block at 0x1A8610C that nothing references by symbol
   (31%, section 4) and the 5 MB menu arena at 0x1340870 (section 3). Add the effect heaps and the
   PMCOM dev link buffers and 85% of `.data` is a dozen big buffers. The named globals the
   simulation actually runs on fit in well under 1 MB.
4. **The 8 MB block is the PS2 main RAM window.** Live code stores PS2 main RAM addresses into it as
   plain literals (0x1C80000, 0x1D00000, 0x1F00000 and others) with no relocation. The block is
   there so those addresses land in memory the process owns. A recreation has to replace each
   literal with a real buffer. [M, section 4]
5. **The relocation table is the reference set.** IDA's fixups and the PE `.reloc` agree exactly,
   283,644 HIGHLOW relocations, 133,148 of them into `.data`, 21,573 distinct targets. An operand IDA
   shows as `unk_...` in `.data` with no relocation under it is a constant, not a reference. [M]

## 2. Method

**Bounds.** The PE section header gives `.data` VA 0xC0A000, virtual size 0x19CC2C4, raw size
0x81600, so the file backed part ends at 0xC8B600. `ida_bytes.is_loaded` agrees, by a binary search
over the section. The neighbours are `.rdata` 0xB0C8C0..0xC0A000 and `.rodata` from 0x25D7000. [M]

**References.** `datamap.py dump` walks every IDB fixup and keeps the 133,148 whose target is in
`.data`, with the instruction or data head that holds it, the function, and whether the access is
a read, a write or an address take (offset, push, data pointer). The fixup list was compared one for
one with the PE `.reloc` directory: same count, same targets. [M]

**Points.** A point is any address a relocation reaches or that a user named. 21,589 points, 3,259
of them user named. IDA also shows `unk_`/`dword_` labels in `.data` that only a non-relocated
immediate reaches. Those are the 602 literal sites of section 4, battle ids, GS register words,
masks, and PS2 addresses, and they are not points.

**Extents.** A point's own size comes from its IDB type, or the access width for an untyped scalar.
If the point is a base (indexed, address-taken, or pointed to from data), its extent runs to the next
point, [I] by construction. A gap of under 16 bytes before the next point is alignment.

**Curated regions.** Big buffers were read by hand and listed in `REGIONS` in `datamap.py` with an
evidence address and a tag (section 3). A region with a name counts as named only when the IDB
carries that name at its start, so the numbers in section 1 are what the IDB says after
`apply_datamap_names.py`, not what this doc proposes.

**Byte classes.** Every byte is named (inside a user named point's extent or a named region),
explained (a placeholder point's extent, an unnamed region, or alignment), or unexplained.

**Subsystem.** In order: the region the address starts, then name rules on the point's own name
(`SUBSYSTEM_RULES`, run with `g_ffx`, `FFX_`, `maybe_` stripped), then a vote of the functions that
reference it. A placeholder function gets the layout law vote of its named neighbours within 16 KB
(FFX's object files are laid out by source file, see `LUA_MODULE.md`), then its callers' vote.

**Threads.** A writer function is tagged with every thread root that reaches it by direct calls or
address takes, from the roots in `THREADS_AND_CLOCKS.md`: S step (`FFX_MainStep 0x820AE0`), I init
(`FFX_MainInit 0x820860`, `FFX_BootSequence 0x6416C0`), W the main thread outside the step
(`_WinMain@16 0x62EE50` minus S), R render 0x616010, J job worker 0x703A20, A FMOD 0x6FA1B0, P
input 0x68D740, L async loader 0x691360, V vsync 0x679C60, T AutoTest 0x6BD1E0, M movie threads
(0xA27CF0, 0xA27F20). `?` is a writer no root reaches directly, nearly always a function reached
through a table (ATEL syscalls, Oef2 ops, vtables). Only direct relocated writes count, a buffer
written through a pointer shows `-`. [M]

Rerun everything with:

```
"C:/Users/Derek/AppData/Roaming/Hex-Rays/IDA Pro/venv/Scripts/python.exe" -I tools/datamap.py dump --idb COPY/FFX.exe.i64 --out dm.json
python -I tools/datamap.py report --dump dm.json --table reversing/DATA_MAP_TABLE.md
```

## 3. Subsystem runs and the explained large gaps

### 3.1 The layout from low to high

Contiguous runs of one subsystem of 64 KB or more, after the regions below. [M]

| range | size | subsystem | what is there |
|---|---|---|---|
| 0xC0A000..0xC8B600 | 0x81600 | mixed | the initialized part. Phyre class and RTTI data, ppp op tables, battle and menu constant tables, CRT state. Three runs of placeholder items over 64 KB, all [M] referenced: 0xC6D448 (92 items referenced only from other data), 0xCA9210 (Phyre shader class descriptors, ~3,900 points) |
| 0xCCCB04..0xCDED31 | 0x1222D | unattributed (Debug, PC port) | `AutoTestManager__ctor` 32 KB stdin buffer at 0xCCCB10, then the PC post effect singletons (Distortion, BlurEffect, BrokenScreen) |
| 0xCEC220..0xD2C268 | 0x40048 | Map effects | the map particle ppp heap |
| 0xD32860..0xD4B470 | 0x18C10 | Magic and effects | the ppp part slot table |
| 0xD524A8..0x1127C80 | 0x3D57D8 | Debug and dev (PMCOM) | the PS2 particle master dev link: receive arena, ring, double buffer, its own ppp heap, send buffers, printf ring. 3.8 MB a retail run never touches |
| 0x112A000..0x1140000 | about 0x16000 | Battle, Save data, Debug, Camera | the dense simulation core: battle state and debug flags, `g_ffxSaveData`, unit pointers, `g_ffxRandStreamState 0x1135EE0`, camera slots 0x1137000.. |
| 0x113FDD0..0x12E95D8 | 0x1A9808 | Magic and effects | resident effect heap, effect packet arena A, the md0 draw work buffer |
| 0x1303704..0x1323D4C | 0x20648 | Debug and dev | SG debug protocol buffers |
| 0x1325000..0x1340000 | about 0x1B000 | Battle, ATEL, Audio | battle work, the ATEL contexts and pad words, sound state, map change state, `g_ffxDebugMode 0x133C910` |
| 0x1340870..0x1840870 | 0x500000 | Menu and UI, Magic and effects | the menu arena, section 3.3 |
| 0x184B610..0x185F610 | 0x14000 | Text and fonts | two font buffers `FFX_Font_InstallFtc` fills |
| 0x1876E00..0x1931208 | 0xBA408 | Map and field | `g_ffxMapWork` |
| 0x1944FFC..0x1984C80 | 0x3FC84 | Phyre engine | class descriptor member storage |
| 0x1984C80..0x1A84C84 | 0x100004 | Movies | the FMV discard block buffer |
| 0x1A8610C..0x2286140 | 0x800034 | PS2 address window | section 4 |
| 0x2286170..0x22C8270 | 0x42100 | Menu and UI | the menu log |
| 0x22C9870..0x23C3640 | about 0xFA000 | Magic and effects, Camera | resident effect objects, camera matrices at 0x2311430.., effect state 0x23114E0..0x2322790, the effect packet cursors, the resident ppp heap |
| 0x23CC3BC..0x2400480 | 0x340C4 | Menu and UI | the world map image |
| 0x2400480..0x25CC580 | 0x1CC100 | Unreferenced tail | 1.8 MB nothing reaches [M] |
| 0x25CC580..0x25D7000 | 0xAA80 | Map, Assets, mixed | map and file system work and the event id table `g_ffxEventIdTable 0x25D5888` |

Bytes by subsystem: PS2 address window 31.0%, Magic and effects 18.5%, Menu and UI 17.2%, Debug and
dev 12.1%, Unreferenced tail 7.0%, Movies 3.9%, Map and field 3.0%, Phyre engine 2.8%, Map effects
1.1%, Battle 0.7%, initialized constant tables 0.5%, everything else under 0.5% each. [M]

### 3.2 Every curated region

These are all the hand read regions, outer before inner. The name column is the IDB name after
`apply_datamap_names.py`, or the name it already had. A `-` is explained with no name of its own.

| range | size | name | subsystem | tag | evidence | what |
|---|---|---|---|---|---|---|
| 0xCEC258..0xD2C258 | 0x40000 | `g_ffxMapFxPppHeapArena` | Map effects | [V] | FFX_MapFx_LoadParticleSet push 40000h 0x72A814 with push offset 0x72A819 | the map particle set's ppp heap, 256 KB, handed to FFX_Ppp_InitHeapForSet |
| 0xD32860..0xD4B460 | 0x18C00 | `g_ffxPppPartSlots` | Magic and effects | [V] | MapFx__f72AC30 memset(0xD32860, 0, 18C00h) at 0x72AC30, PppCore__f72C1B0 indexes 396 * slot | 256 ppp part slots of 396 bytes, the PS2 partmast part table |
| 0xD524A8..0xE524A8 | 0x100000 | `g_ffxPmcomRecvArena` | Debug and dev | [V] | sub_760610 0x760621 resets the bump cursor dword_D524A0 to it, sub_760080 bumps it | PMCOM (PS2 particle master protocol) receive arena, 1 MB, dev tool link only |
| 0xE524F0..0xF524F0 | 0x100000 | `g_ffxPmcomRing` | Debug and dev | [V] | sub_762F50 wraps dword_D524A4 at byte_F524F0 (0x762F6C), sub_761E20 reads a 512x416 frame into its start | PMCOM ring, 1 MB. Its first 0xD0000 also takes the 512x416 screen grab |
| 0xF224F0..0xF524F0 | 0x30000 | - | Debug and dev | [V] | sub_761E20 converts the grab at 0xE524F0 into RGB here and sends it in 4 KB IM packets | the RGB copy of the PMCOM screen grab, inside the ring |
| 0xF524F0..0xFD24F0 | 0x80000 | `g_ffxPmcomDoubleBuffer` | Debug and dev | [V] | sub_760080 0x7601CB: byte_F524F0 + (dword_D4B4C0 & 1) << 18 | PMCOM double buffer, two halves of 0x40000 |
| 0xFD24F0..0x10D24F0 | 0x100000 | `g_ffxVfxPppHeapArena` | Magic and effects | [V] | sub_760610 0x760615 and sub_761D60 0x761DBA: FFX_Ppp_InitHeapForSet(dword_10D4CFC, 0, 0xFD24F0, 100000h) | the PMCOM effect slots' ppp heap, 1 MB |
| 0x10D5654..0x10D7640 | 0x1FEC | - | Debug and dev | [I] | sub_76B210 and sub_76B3D0 memcpy the outgoing payload here | PMCOM send staging |
| 0x10D7640..0x10E7640 | 0x10000 | - | Debug and dev | [I] | sub_76B2D0 0x76B2EC stores it at driver+0x14, the span to the next reference is 0x10000 | PMCOM protocol driver buffer |
| 0x10E7880..0x1127C80 | 0x40400 | `g_ffxDevPrintfRing` | Debug and dev | [V] | sub_76B860 0x76B88D imul 404h, index wraps at 256 | 256 vsprintf lines of 1028 bytes for the dev link |
| 0x112CA90..0x1133350 | 0x68C0 | `g_ffxSaveData` | Save data and party | [V] | FFX_SaveFile_InstallBlock 0x8B54A0 memcpy(dest, image + 0x40, 0x68C0), SAVE_BLOCK_MAP.md tiles it | the save block, 26,816 bytes. Hash visible, the IDB types only its first word |
| 0x113FE10..0x119FE10 | 0x60000 | `g_ffxResidentEffectHeapArena` | Magic and effects | [V] | sub_7FD760 push 60000h 0x7FD78F with push offset 0x7FD794 | resident effect unit 0 heap, 384 KB, set up once at boot |
| 0x11A1260..0x12A12F0 | 0x100090 | `g_ffxEffectPacketArenaA` | Magic and effects | [V] | sub_7EC890 0x7EC8A9 stores it as dword_2332E90, FFX_MagicResident_OvlEffectsPass 0x80069D adds 0F0000h for the limit | per frame effect packet arena. The sphere grid board effect file is loaded at its start too (typed 0x5EDA0) |
| 0x12A8DA8..0x12E95D8 | 0x40830 | - | Magic and effects | [I] | stored at prim+0x180 by sub_A6ACB0 0xA6B009, sub_A6B220, sub_A6C3D0, op_md0_onef, and at +0x10 by sub_A73D90 | a draw work buffer the md0 primitive builders point at, span to the next reference |
| 0x1303D40..0x1313D40 | 0x10000 | - | Debug and dev | [I] | sub_84EDE0 0x84EDEA (KSEG1 alias +0x20000000) | SG debug protocol buffer A |
| 0x1313D40..0x1323D40 | 0x10000 | - | Debug and dev | [I] | sub_84EDE0 0x84EDFB | SG debug protocol buffer B |
| 0x1340870..0x1840870 | 0x500000 | - | Menu and UI | [V] | FFX_MenuSys_RunFrame push 500000h 0x8AB0D2, memset of the whole span | the menu system arena, 5 MB, cleared when the menu system starts. Battle effect work reuses parts of it. It has no name of its own because its first sub-buffer already owns the address |
| 0x1340870..0x1410870 | 0xD0000 | `g_ffxMenuSysGsBackupBuffer` | Menu and UI | [V] | sub_8AA690 reads 13 GS chunks into it | menu GS backup, the first 0xD0000 of the menu arena |
| 0x1410870..0x14C0870 | 0xB0000 | - | Menu and UI | [I] | sub_8A9510 0x8A952E and sub_8AAC80, 13 chunk loops | second GS save area |
| 0x14C0870..0x14E0870 | 0x20000 | - | Menu and UI | [I] | getter sub_8BEB20 / sub_8BED90 | menu work buffer |
| 0x14E0870..0x1540870 | 0x60000 | - | Menu and UI | [I] | getter sub_8BEB50 | menu work buffer |
| 0x1540870..0x1574F70 | 0x34700 | - | Magic and effects | [I] | FFX_Oef2_Op132 pushes it twice | Oef2 op 132 work buffer inside the menu arena |
| 0x1574F70..0x1576F70 | 0x2000 | - | ATEL and events | [I] | FFX_Menu_SetupAtelContext passes it to FFX_Atel_SetupStaticContext(6, ...) | menu ATEL context work |
| 0x157F130..0x1586F70 | 0x7E40 | - | Menu and UI | [I] | TOSwapInternationalEnvExecMenu loads group 13 into it | international env menu file |
| 0x1586F70..0x158EF70 | 0x8000 | - | Menu and UI | [I] | FFX_MenuSys_RunFrame sa_task__f77A1D0(2, it, 0) | sa_menu task work |
| 0x15A0870..0x15C0870 | 0x20000 | - | Menu and UI | [I] | FFX_Module0_ExecBoot sub_88D190(42, 0, it) | boot module file buffer |
| 0x15C0870..0x1640870 | 0x80000 | `g_ffxMenuGsPacketBuffer` | Menu and UI | [V] | sub_8BEF30 push 80000h 0x8BEF35, sub_9047C0(it, 80000h, 480000) | menu GS packet buffer, 512 KB |
| 0x1640870..0x16931A0 | 0x52930 | - | Magic and effects | [I] | FFX_Oef2_ExplodeModel, getters sub_8A9BA0 / sub_8BEA30 | explode fragment work inside the menu arena |
| 0x16931A0..0x16AB1A0 | 0x18000 | - | Menu and UI | [I] | menu work +71272 (FFX_Menu_Build, FFX_SphereGrid_StepNodeKindChange) | sphere grid node kind work |
| 0x16AB1A0..0x16AD8B0 | 0x2710 | `g_ffxMenuPageRecords` | Menu and UI | [V] | sub_A572E0 0xA572F0 rep stosd 9C4h dwords, then g_ffxMenuPages = it | menu page records, 0x2710 |
| 0x16AD8B0..0x16C1870 | 0x13FC0 | `g_ffxMenuWorkBlock` | Menu and UI | [V] | sub_A572E0 0xA57333 rep stosd 4BF0h dwords, then g_ffxMenuWork = it | menu work block, 0x12FC0 |
| 0x16C1870..0x173E870 | 0x7D000 | `g_ffxSphereGridPppHeapArena` | Menu and UI | [V] | FFX_SphereGrid_StepWarp push 7D000h 0xA48079 | sphere grid ppp heap, 0x7D000. The IDB types 0x6F000 and the heap runs over g_ffxBtlMixPrepareTable 0x1730870, which only battle uses |
| 0x1740870..0x1760870 | 0x20000 | - | Menu and UI | [I] | FFX_SphereGrid_LoadLayout loads the board layout file into it | sphere grid layout file |
| 0x1760870..0x17C0870 | 0x60000 | `g_ffxMenuEffectPacketBuffer` | Menu and UI | [I] | sub_A545A0 and FFX_Menu_DrawAllPartsIgnoringCatchUp set g_ffxVfxPacketCursor to it | menu effect packet buffer |
| 0x17C0870..0x1840870 | 0x80000 | - | Magic and effects | [I] | sub_7F73D0 stores it at +4 of an effect mesh record | effect mesh work, the last 512 KB of the menu arena |
| 0x1876E00..0x19311E8 | 0xBA3E8 | `g_ffxMapWork` | Map and field | [V] | sub_90CD90 0x90CDC8 writes [esi+0BA3E0h] with esi = &g_ffxMapWork, the largest field offset seen | the map work struct, about 745 KB: 72 + 72 records of 0x460 (0x908F50, 0x908940), the map out header g_ffxMapOut at +0x9CAA8 |
| 0x1945140..0x1984C80 | 0x3FB40 | - | Phyre engine | [I] | PDeferredLighting*, PDepthOfField*, PMotionBlur*, PMeshParticleSystemBase ClassDescriptor_ctor push these | Phyre class descriptor member storage, three arrays per class |
| 0x1984C80..0x1A84C80 | 0x100000 | `g_ffxFmvDiscardBlockBuffer` | Movies | [V] | PVideoPlaybackWin32_readAllBlocks 0xA277A0, MOVIE_DECODE.md | 1 MiB scratch the movie reader throws blocks into |
| 0x1A8610C..0x2286140 | 0x800034 | `g_ffxPs2AddressWindow` | PS2 address window | [M] | no relocation in the whole span (the PE .reloc and the IDB fixups agree), the literals in section 4 land in it | 8 MB that nothing references by symbol. Live code writes PS2 main RAM addresses 0x1C80000..0x2000000 into it as literals |
| 0x2286170..0x2288170 | 0x2000 | - | Menu and UI | [I] | sub_A5DDA0 0xA5DDF9 stores it as dword_2286140 | menu log head |
| 0x2288170..0x22C8270 | 0x40100 | - | Menu and UI | [V] | nofunc 0xA5E350: ((dword_2286168 - n) & 1Fh) << 0Dh indexes 17 fields at 0x2288230..0x228A230 | 32 records of 0x2000 read back by the menu log printer |
| 0x22DB170..0x22FB180 | 0x20010 | `g_ffxResidentEffectObjects` | Magic and effects | [I] | unit 0 object pointer at 0xC88AA4, FFX_Effect_InitUnit(.., 0x200, ..) from sub_7FD760 | 512 effect objects of resident unit 0 (YONISHI_ENGINE.md 7.3) |
| 0x23227E4..0x2332860 | 0x1007C | - | Debug and dev | [M] | no relocation and no literal lands in it. byte_23227E3 just before is the SG debug 'effect target memory save' flag | dead: 64 KB nothing in the binary reaches |
| 0x2333500..0x2343520 | 0x10020 | `g_ffxOef2Op078FallbackBuf` | Magic and effects | [I] | FFX_Oef2_Op078 0x80F2AE uses it when sub_7B7EB0 returns null | Oef2 op 78 fallback buffer |
| 0x2343520..0x23C3520 | 0x80000 | `g_ffxResidentPppHeapArena` | Magic and effects | [V] | FFX_MagicResident_OvlInit push 80000h 0x8005C6 with push offset 0x8005CB | resident magic ppp heap, 512 KB |
| 0x23CC480..0x2400480 | 0x34000 | `g_ffxMenuWorldMapImage` | Menu and UI | [I] | FFX_Menu_ExecModule23WorldMap 0x8E299C loads menu group 23 into it and uploads 512x416 PSMT8, the file is 212,992 bytes | world map image load buffer, sized to the file |
| 0x2400480..0x25CC580 | 0x1CC100 | - | Unreferenced tail | [M] | no relocation lands in it and the only literals that do are constants, not addresses | 1.8 MB past the world map image that nothing in the binary reaches |

The four unnamed runs over 64 KB the report explains by "referenced items" rather than a region are
in the 3.1 table: 0xC6D448, 0xCA9210, 0xCCCB04 and 0x184B610. Each is a run of referenced points with
placeholder names, so its bytes count as explained, and `DATA_MAP_TABLE.md` lists none of them because
the table only lists user named points.

### 3.3 The menu arena, 0x1340870..0x1840870

`FFX_MenuSys_RunFrame` pushes 500000h at **0x8AB0D2** and clears the whole 5 MB when the menu system
starts [V]. Its first 0xD0000 is `g_ffxMenuSysGsBackupBuffer`, the GS backup `sub_8AA690` reads
13 chunks into, which is why the arena itself has no separate name. The menu, the sphere grid and
some battle effect work share the arena by time. Two overlaps are real and harmless only because
the users never run at once:

- `g_ffxSphereGridPppHeapArena 0x16C1870` is initialised with 0x7D000 bytes (push 7D000h at
  **0xA48079** [V]), so it runs over `g_ffxBtlMixPrepareTable 0x1730870`, a battle buffer.
- Oef2 op 132 (0x1540870), the explode fragment work (0x1640870) and the effect mesh work
  (0x17C0870) are effect users inside the menu arena [I].

## 4. The PS2 address window and the literals

**0x1A8610C..0x2286140 (0x800034 bytes) has no relocation at all.** Not a single fixup lands in it,
in the IDB or in the PE `.reloc` [M]. It is not dead. 602 instruction operands resolve into `.data`
with no relocation under them, and 146 of them land in this window [M]. Most are constants that
happen to look like addresses (0x2000000 x43 is a flag bit, 0x1FFFFFF x16 a mask, 0x2020200 a colour).
The rest are PS2 main RAM addresses that the port kept as literals:

| literal | used by | what |
|---|---|---|
| 0x1C80000, 0x1CA0000, 0x1CC0000 | `sub_901100`, `sub_8C2CE0`, `sub_901160` | texture sources passed to `FFX_Gs_UploadTexture` |
| 0x1D00000 | `sub_7EC890`, stored at **0x7EC8B5** into `g_ffxEffectPacketArenaBBase` [V] | effect packet arena B. `opu_part_run` bumps `g_ffxEffectPacketCursorB` through it (0x80BCB1, 0x80BCF2) |
| 0x1F00000 | `FFX_MenuSys_DrawAllWindows`, `FFX_BtlOd_WakkaReels_Draw`: `sub_9047C0(0x1F00000, 0x100000, 983040)` | a 1 MB GS packet buffer, reached from the step. Also `FFX_Oef2_Op084`, `sub_A74D30`, and a bound in `movie_str` and `sub_803220` |
| 0x1F00008..0x1F00090 | code at 0xA5F6BD with no function | stores into the start of the same 1 MB |
| 0x1F80000 | `sub_7F65D0`, `sub_A6DBE0` | a buffer inside the 1 MB at 0x1F00000 |
| 0x1FA0040 | `movie_str__f77DF90`, `sub_77CDA0` | movie string work |
| 0x1B00000, 0x1B0003C | `sub_760F30` (PMCOM), `FFX_Yanagi_InitProgram`, `sub_936340` | dev paths |

So the window is the address space those PS2 literals point into, kept in the image so that writes
through them hit memory the process owns. The live literals run 0x1C80000..0x2000000 and the window
covers 0x1A8610C..0x2286140 at the preferred base, so they stay inside it for any load slide from
about -0x286000 to +0x1F9000 [I]. 0x1B00000 sits outside the window only when the image loads high,
and its users are dev paths. The window is named `g_ffxPs2AddressWindow` in the IDB now.

**The two effect packet arenas.** `sub_7EC890` sets both bases: A is `g_ffxEffectPacketArenaA
0x11A1260`, a real relocated buffer, and B is the literal 0x1D00000 [V 0x7EC8AB, 0x7EC8B5]. Arena A's
limit is base + 0F0000h (**0x80069D** [V]). B has no limit store, its cursor only moves forward inside
`opu_part_run`.

## 5. Port notes

### 5.1 Big arrays the recreation must size

Simulation and presentation. Sizes are [V] where the region says so in 3.2.

| buffer | size | needed | note |
|---|---|---|---|
| `g_ffxMapWork 0x1876E00` | at least 0xBA3E4 (the store at **0x90CDC8** writes `[esi+0BA3E0h]`) | yes, simulation | the map runtime struct, 72 + 72 records of 0x460, `g_ffxMapOut` at +0x9CAA8. The IDB types it as 0x70, ignore that |
| `g_ffxSaveData 0x112CA90` | 0x68C0 | yes, hash visible | `SAVE_BLOCK_MAP.md` |
| menu arena 0x1340870 | 0x500000 | yes | lay the sub-buffers out explicitly, section 3.3, keep the two time-shared overlaps in mind |
| `g_ffxMapFxPppHeapArena 0xCEC258` | 0x40000 | yes | presentation |
| `g_ffxPppPartSlots 0xD32860` | 256 x 396 | yes | presentation |
| `g_ffxResidentEffectHeapArena 0x113FE10` | 0x60000 | yes | presentation |
| `g_ffxEffectPacketArenaA 0x11A1260` | 0xF0000 | yes | per frame, presentation |
| effect packet arena B (literal 0x1D00000) | unknown, no limit store | yes | give it a real buffer. [I] the PS2 layout leaves 0x1D00000..0x1F00000, 2 MB |
| menu GS packets (literal 0x1F00000) | 0x100000 | yes | `sub_9047C0`'s size argument |
| GS texture sources 0x1C80000.. | 0x20000 apart | yes | three buffers |
| `g_ffxResidentEffectObjects 0x22DB170` | 512 x 256 | yes | IDB comment |
| `g_ffxResidentPppHeapArena 0x2343520` | 0x80000 | yes | presentation |
| `g_ffxMenuWorldMapImage 0x23CC480` | 0x34000 | yes | the file is 212,992 bytes |
| `g_ffxFmvDiscardBlockBuffer 0x1984C80` | 0x100000 | movie layer only | written by the movie reading thread |
| PMCOM, SG debug, dev printf, `g_ffxVfxPppHeapArena` | about 4 MB in all | no | dev tool link only |
| 0x2400480..0x25CC580, 0x23227E4..0x2332860 | 1.8 MB, 64 KB | no | nothing reaches them [M] |

### 5.2 Globals written off the step thread

The report lists every named global with a direct writer that a non-step root reaches. Grouped [M]:

- **W, the main thread outside the step:** Phyre's window, timer and command line globals
  (`g_phyreMainHwnd`, `g_phyreTimeOrigin`, `g_phyreFixedTimeStepArg` ...), `g_ffxTargetFrameTime`,
  `g_ffxSteamApiReady`, `g_ffxGameSettings`, `g_phyreFrameTickCount`. None is simulation state.
- **A, FMOD:** `g_ffxFmodMusicBlocked 0xCEC174` and `g_ffxFmodVoiceBusyDisabled 0xCEC184`, written
  by `FmodVoice__dataChange` and `FmodVoice__resetForMap`.
- **P, input:** `g_ffxInputPadR1HeldAtPoll 0x112CA20` by `FFX_Input__poll`, and `g_ffxFmvState` through
  the focus lost handler.
- **M, movie threads:** the Ogg, Mkv and vpx allocator hooks (`g_oggMalloc` .., `g_mkvMalloc` ..,
  `g_vpxMemFuncs`), also written from S and I.
- **T, AutoTest:** `g_ffxBattleDisabled 0x112CA2C` by `AutoTestManager__execCommand`, the event id
  table, and through the commands it runs, step functions like `FFX_Ch_SetPlayerChr` and
  `FFX_Map_RequestChange`. AutoTest is a dev harness, but a port must not let it write simulation
  state from its own thread.
- Buffers written through pointers do not show up here. The one that matters is
  `g_ffxFmvDiscardBlockBuffer`, written by the movie reading thread (`MOVIE_DECODE.md`).

### 5.3 Hash visible

- `g_ffxSaveData 0x112CA90..0x1133350`, 26,816 bytes (`GAME_STATE.md`, `SAVE_BLOCK_MAP.md`).
- `g_ffxRandStreamState 0x1135EE0`, 68 dwords, outside the save block, written only by
  `FFX_Rand_Stream` and `FFX_Rand_SeedAllStreams` (S and I).
- The other simulation globals in 0x112A000..0x1140000 and 0x1325000..0x1340000 (battle, ATEL, map
  change state) per `GAME_STATE.md` section 6.
- Not hash visible [I]: every arena in 5.1 except `g_ffxMapWork` and the save block. The effect
  heaps, packet arenas, menu arena, FMV buffer and the PS2 window hold presentation and dev state.

## 6. Corrections to other docs

1. `SETTINGS_MENU.md` section 10 said the uninitialised tail of `.data` runs 0xC0A000..0x25D7000.
   That is the whole section. The zero fill starts at 0xC8B600. Fixed.
2. `CONFIG_ROWS.md` put `g_ffxMenuConfigCursorRow` at 0x186A440. It is **0x186A444**, read with
   `movsx ecx, g_ffxMenuConfigCursorRow` at 0x8CB18C [V]. 0x186A440 is a different dword,
   `dword_186A440`, written at 0x8CB248. `MENU_SYSTEM.md` already had 0x186A444. Fixed.

An audit of every "name 0xADDR" pair in `reversing/*.md` against the IDB found no other wrong
`.data` address. The many 0x400000 differences (`BLITZBALL.md`, `CAMERA_BUILD.md`,
`CHEAT_BATTLE.md`, `MINIGAMES_TIMED.md`, `INPUT_LAYER.md` and others) are RVAs, and each of those
docs says so at the top.

Things the IDB has wrong that this pass did not change:

- `g_ffxMapWork` is typed `_DWORD[28]` (0x70). The struct is at least 0xBA3E4.
- `g_ffxSaveData` is typed as one word. The block is 0x68C0.
- `g_ffxSphereGridPppHeapArena` is typed 0x6F000, the heap is initialised with 0x7D000.
- The repeatable comment on `g_ffxMenuSysGsBackupBuffer 0x1340870` still describes the "builtin
  effect overlay table" that `EFFECT_OVERLAY.md` 2.5 showed it is not.

## 7. Open items

- `maybe_FFX_Yonishi__feedPadStateBothPorts` (around 0x7FF739) reads both packet arena bases and
  resets both cursors. That is an effect packet reset, the pad name looks wrong. Not renamed here.
- The size of effect packet arena B. Nothing stores a limit. 2 MB is the PS2 layout's room, not a
  measured bound.
- 0x1F80000 sits inside the 1 MB that `sub_9047C0(0x1F00000, 0x100000, ..)` hands out. Whether the
  two users can be live in one frame is not checked.
- The menu arena sub-buffers tagged [I] are sized to the next reference. A reader's loop bound would
  promote each.
- The largest unexplained runs left are all under 16 KB: 0x22FD6C4 (0x401C, after
  `g_ffxShadowFlagPtrA`), 0xC8B508 (0x4000, the last initialized bytes), 0x1935200 (0x3EF0),
  0x10D2534 (0x27BC), 0xC3E7AC (0x266C, after `g_ffxPppOpTablePm`).
