# FFX-2.exe reversing recon

Target: `FFX-2.exe` from the same Steam "FINAL FANTASY FFX&FFX-2 HD Remaster" build that
`FFX.exe` comes from. IDB at
`G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\FFX-2.exe.i64`.

The question this pass answered: **does FFX-2 leave the same PS2 source and function names behind
that FFX does?** Yes. Every naming vector that worked on `FFX.exe` is present in `FFX-2.exe`, at the
same magnitude. The two binaries are siblings out of one Perforce depot, built the same day.

## Header, side by side with FFX

| | FFX.exe | FFX-2.exe |
|---|---|---|
| preferred base | `0x400000` | `0x400000` |
| entry RVA | `0x5493C1` | `0x47DA17` |
| build stamp (`__DATE__`) | 2026-09-10 | 2026-09-10 |
| PDB path | `E:\FFXX-2\P4\hg_code\ffx_w32\D3D11Final\FFX.pdb` | `E:\FFXX-2\P4\hg_code\ffx2_w32\D3D11Final\FFX-2.pdb` |
| engine | PhyreEngine `3.9.0.0 "SacSlicer"` | PhyreEngine `3.9.0.0 "SacSlicer"` |
| Lua | 5.2.1 | 5.2.1 |
| functions | 49,088 | 51,245 |
| RTTI type descriptors | 2,086 | 2,120 (1,892 of them `Phyre::`) |
| vtables | 2,031 | 2,067 |
| class hierarchy descriptors | 2,081 | 2,117 |

Same depot (`E:\FFXX-2\P4\hg_code`), sibling project (`ffx2_w32` against `ffx_w32`), same
`D3D11Final` config. So the whole FFX technique stack applies unchanged: Phyre reflection metadata,
RTTI-driven vtable attribution, `PClassDescriptor<T>` naming, the Lua alphabetical-layout law.

Assume nothing transfers by address. Everything has to be re-derived in this IDB.

IDA segments: `.text 0x401000-0xC25000`, `.idata 0xC25000-0xC258CC`, `.rdata 0xC258CC-0xD27000`,
`.data 0xD27000-0x1E1D000`, `.rodata 0x1E1D000-0x1E1E000`, `_RDATA 0x1E1E000-0x1E1F000`. As with
FFX, `.idata` is an IDA split out of `.rdata`, not a real PE section.

## The `__FUNCTION__` assert triple is here too

FFX's single richest naming source is PhyreEngine's scripting headers storing `__FILE__`, `__LINE__`
and `__FUNCTION__` into a global triple before asserting. FFX-2 has the identical construct:

| global | role |
|---|---|
| `0x1A3A9F8` | `__FILE__`, 1,565 xrefs, 1,550 of them writing the one path `e:\ffxx-2\p4\hg_code\middleware_w32\phyreengine\include\Scripting/PhyreScripting.inl` |
| `0x1A3A9FC` | `__LINE__`, 1,566 xrefs |
| `0x1A3AA00` | `__FUNCTION__`, 1,565 xrefs |

Harvesting it: the write sites resolve into **781 distinct functions, every one of which agrees on a
single `__FUNCTION__` value, with zero conflicts and zero orphan sites.** FFX got 772 the same way.
All 781 were placeholders before this pass. 775 are
`Phyre::PScripting::PScriptAccessors::PObjectAccessor<T>::Get` and 6 are plain scripting handlers
(`IndexHandler`, `NewIndexHandler`, `VectorIndexHandler`, `VectorNewIndexHandler`,
`MatrixNewIndexHandler`, `PhyreGetTypeOf`).

Applied as `PObjAcc_<T>__Get` and `Phyre__<name>`, with `::` to `__`, `&` to `_ref`, `*` to `_ptr`,
and `const` kept so the const and non-const overloads stay distinct. Keeping `const` matters:
dropping it collides 3 pairs. Each function carries a repeatable comment naming the triple and the
exact `__FUNCTION__` string.

The value here is the type argument, not the function body. These are one-line accessor stubs, but
`PObjAcc_Phyre_PGame_PGameSettings_ptr__Get` tells you `Phyre::PGame::PGameSettings` is script
exposed, and the set of 775 is effectively the script-visible type list.

## The PS2 source tree is embedded

57 source filename strings survive in `.rdata`. The directory names are the interesting part,
because they are the original PS2 project layout:

| path prefix | what it is | FFX equivalent |
|---|---|---|
| `..\ffx2\ps2\common\tklib.c` | the PS2 toolkit | `tklib.c` |
| `..\program2\battle\kz*.c` | FFX-2 battle: `kzcamseqtask`, `kzchardata`, `kzitem`, `kzlmbattle` | FFX uses a different battle tree |
| `..\proj\src\chr\chr.c`, `cache.c` | the CHR actor system | same |
| `..\proj\src\main\mem.c` | allocator | same |
| `..\src\rcBase\`, `rcBg\`, `rcData\`, `rcFlex\`, `rcUI\` | the `rc*` libraries, 24 files | same |
| `..\sasa\lib\src\` | movie and CD streaming, 7 files | same |
| `..\sound\Ffx2Fmod.cpp`, `FmodMusic.cpp`, ... | audio, 6 files | `FmodMusic.cpp` etc |
| `..\result\mresult.c`, `?..\npart\eiProg\pppYanagi.c`, `kydng.c` | results screen, particles, dungeon | FFX-2 only |

`kzlmbattle.c` and the `KzLm*` functions are **Last Mission**, which ships inside `FFX-2.exe`
alongside X-2 proper. `kydng.c` and `DgnSyncREff2` are the dungeon (Via Infinito).

**139 placeholder functions were attributed to a translation unit** by their xref to one of these
strings, named `<module>__f<ADDR>` exactly as FFX's 199 were. That is module attribution only and
implies nothing about behaviour.

A note for anyone wanting to repeat FFX's Lua win here: the alphabetical-layout law needs a real
symbol list to map onto a known address range. These 57 files give you ranges to anchor, but there is
no published symbol list for `rcBg` or `kz*` the way there is for Lua 5.2, so that route does not
generalise to the game code without a second source of names.

## PS2 function names recovered from log strings

The Virtuos port kept its debug logging call sites. The log body is stubbed (`nullsub_28` at
`0x49AE50`, 2,087 call sites) but **the format strings are still passed**, and many name their own
function. Mining strings of the form `name()` for PS2-shaped identifiers gave 363 candidates, of
which 46 resolved to a referencing function and **29 survived a strict bijective filter** (the name
maps to one function and that function maps back to only that name). 6 more came from entry and exit
trace labels. 35 names applied in total, each with a comment recording its evidence:

| name | VA | how |
|---|---|---|
| `Ch_Init` | `0x6D74F0` | prints `sizeof(CHR)=%x` |
| `Ch_SetRot` | `0x6E6540` | prints the Virtuos invalid-rotation warning, verified by body |
| `Sg_PcWrite` | `0x6EA460` | passes `"Sg_PcWrite"` to the logger, verified by body |
| `CdEnvInit` | `0x538BB0` | entry banner `=== CdEnvInit()` |
| `movie_init` | `0x537D80` | tail-logs `----  movie_init()  ------` |
| `chEnGravity` | `0x70D230` | subject of its own deprecation warning |
| `KzCharaData_ChangeLevel`, `_Composition`, `_EquipOff`, `_SetHp`, `_SetMp`, `_SetOversoul` | `0x5FEAC0`, `0x5FF160`, `0x6004D0`, `0x6018B0`, `0x601A10`, `0x601A70` | `Entry.` banners |
| `KzLmBattleInit`, `_Update`, `_Exit` | `0x5FBC60`, `0x5FBD50`, `0x5FBC30` | `Entry` banners |
| `KzLmSaveData_Make`, `_RebuildObject` | `0x603B70`, `0x603D00` | banners |
| `DgnSyncREff2` | `0xB590B0` | `ky:DgnSyncREff2() call handle 0x%x` |
| `opu_part_run` | `0x6BA5D0` | unique xref, 2,906 bytes |
| `op_oef2_texture_load` | `0x67E010` | Virtuos null-texture error |
| `op_gs24_clear`, `op_ot_add_after`, `op_ot_add_before` | `0x67F840`, `0x680940`, `0x680950` | "call to yonishi X() skipped" stubs |
| `pppFpLoop`, `pppSetFpPdt` | `0x810190`, `0x810FB0` | unique xref |
| `rcuiMakeBasePath` | `0x7E43E0` | unique xref |
| `yiFileIO_readmalloc`, `yiImage_GSmallocimage` | `0x7BC8B0`, `0x7C7170` | unique xref |
| `hdd_install_check`, `hdd_install_end`, `hdd_thread_main` | `0x53E080`, `0x53E1B0`, `0x53E510` | banners |
| `hdd_file_check`, `hdd_format_check`, `hdd_ins_check`, `hdd_mount`, `hdd_unmount` | `0x53D370`, `0x53D6B0`, `0x53DBE0`, `0x53E380`, `0x53E6B0` | bracketing `_in` and `_out` trace labels |

### Two traps in that pass, both real

**A function that logs several of these names is not named by any of them.** `0x703F70` references
`TkAnti()`, `op_et_bindeff_all()` and `op_et_bindeff_off_all()`. It is a 3,690-byte switch dispatcher
on a message id, so it is none of the three. The strict bijective filter is what catches this, and
without it you get three confidently wrong names. Same for the `chEnGravity` warning string, which
names two functions and is printed by the first.

**IDA's CRT signature matches are not all correct.** `0x537D80` was named `__cfltcvt_init` by
signature match. The body zeroes a block of movie globals and tail-logs
`----  movie_init()  ------`. It is `movie_init`, and the CRT name is a false positive. There is at
least one more, a `_cfltcvt_init_5` called from `hdd_mount`. **Do not trust a CRT name in this IDB
without reading the body.**

The second argument to `nullsub_225` (`0x7BBEB0`, 35 call sites) looked at first like a
`__FUNCTION__` parameter, because `Sg_PcWrite` passes its own name there. It is not. Other call sites
pass category labels (`"movie running ..."`, `"EFFECT SYNC ERROR"`, `"SG:Add MGRP"`), so that
argument is a log title and only sometimes the function name. Not a usable bulk source.

## `CHR` is the same struct but it is NOT the same size

This is the finding that matters most if the co-op work is ever pointed at FFX-2.

`Ch_Init` at `0x6D74F0` prints `sizeof(CHR)=0x910`, which is **2,320 bytes**. FFX's `FFX_Ch_Init`
prints `0x880`, which is 2,176. **FFX-2's CHR is 144 bytes wider.**

What is confirmed to match so far is `m_rotY` at `+0x158`. `Ch_SetRot` at `0x6E6540` writes
`*(float *)(chr + 344)`, validates it, and on rejection zeroes the field and prints the same
`"VIRTUOS WARNING: got invalid rotation from atelscript in Ch_SetRot()!!"` string FFX has. 344 is
`0x158`, which is exactly where `ffx_types.h` puts `m_rotY`.

So the early fields are shared and the layout diverges somewhere after. **Do not apply FFX's
`ffx_types.h` `CHR` to this IDB wholesale.** Re-derive the tail, and treat every offset past the
first divergence as unknown until checked.

One incidental find while reading `Ch_SetRot`: FFX-2 carries a map-specific hack keyed on the map
names `"n303"` and `"n311"` that caches a CHR pointer in the single global `0x116E3C8` and copies
rotation state out of it. That is a one-slot global cache of exactly the kind the FFX
single-instance-globals audit is about, so if FFX-2 ever gets the same treatment it belongs on the
list.

## State of the IDB after this pass

| | before | after |
|---|---|---|
| named functions | 2,972 | 3,927 (7.7%) |
| placeholders | 48,273 | 47,318 |

Added: 781 from the `__FUNCTION__` triple, 139 source-file attributions, 35 PS2 log-string names,
plus `Ch_Init`. Database saved.

### What is still untouched, highest yield first

The mechanical sources that produced the bulk of FFX's 20,077 names have **not** been run here yet.
In rough order of expected return:

1. **The RTTI-driven vtable pass.** 2,067 vtables and a complete `??_R3`/`??_R2`/`??_R1` hierarchy
   are present. On FFX this alone produced 8,234 `<Class>__vf<NN>` names, with shared slots resolved
   to the declaring base class. This is the single biggest remaining win and it is purely mechanical.
2. **`PClassDescriptor<T>` naming.** 1,266 `PClassDescriptor` strings are present, which on FFX gave
   1,975 `__GetStaticClassDescriptor`, 686 `__GetClassDescriptor` and 677 `__GetClassName`.
3. **Phyre reflection metadata** for real struct layouts with original field names. The extractor
   already exists at `extract_phyre_reflection.py` and should need only a retarget.
4. **`PMethodCallerConcrete<Class, Ret, Args...>` vtables** for reflected methods with full
   prototypes, 258 on FFX.
5. **Lua 5.2.1**, same version as FFX, so the alphabetical-layout technique in `LUA_MODULE.md`
   transfers directly once the module range is located.

Nothing in this pass touched the data archives. `FFX2_Data.vbf` was never scanned by the FFX work
either, see the note in `LUA_MODULE.md`.
