# FFX.exe reversing notes

Target: `FFX.exe` from the Steam "FINAL FANTASY FFX&FFX-2 HD Remaster" build.

- 32-bit x86 PE, **preferred** base `0x400000`, entry RVA `0x5493c1`, built with MSVC 2012
  (MSVCR110 / MSVCP110). Build stamp from the embedded `__DATE__`/`__TIME__` strings:
  **2026-09-10**. PDB path in the debug directory (the PDB itself is not shipped):
  `E:\FFXX-2\P4\hg_code\ffx_w32\D3D11Final\FFX.pdb`, so the project was `ffx_w32`, config
  `D3D11Final`.
- **The exe is ASLR-relocated.** `DllCharacteristics = 0x8140` sets `DYNAMIC_BASE`, `RELOCS_STRIPPED`
  is not set, and a full `.reloc` ships. It does *not* load at `0x400000` - measured at runtime it
  came up at `0x00510000`, a slide of `+0x110000`. Every address in this folder and in the IDB is a VA
  at the preferred base, which is what IDA shows. **Runtime code must compute
  `GetModuleHandle(NULL) + (VA - 0x400000)`.** See `../loader/` for the hook addresses as RVAs.
  - Two traps confirmed by actually running it. **ASLR is per-boot, not per-launch**: consecutive
    launches both used `0x00510000`, so a hardcoded VA appears to work until the machine reboots.
    And the loader **rewrites `OptionalHeader.ImageBase` in the in-memory header** to the real load
    address, so code that sanity-checks it against the on-disk `0x400000` fails every time.
- Engine: Sony PhyreEngine `3.9.0.0 "SacSlicer"` (`Phyre::` namespace). Bullet for physics, Iggy
  (RAD's Flash player) for menus, FMOD for audio, D3D11 renderer, Lua 5.2 for scripting, statically
  linked zlib 1.2.8, libwebm/mkvparser for video.
- **PE sections** are `.text`, `.rdata`, `.data`, `.rodata`, `_RDATA`, `.rsrc`, `.reloc`. Note IDA
  additionally splits an `.idata` segment (`0xb0c000`-`0xb0c8c0`) out of `.rdata` for the import
  area - that is an IDA segment, not a real section. `.text` spans `0x401000`-`0xb0c000`.
- No `.bind` section, so there is no Steam DRM wrapper. The import table is directly patchable.
- 48,986 functions. Full MSVC RTTI is present: 2,086 type descriptors, 2,031 vtables,
  2,028 complete object locators, and 2,081 class hierarchy descriptors (so the full inheritance
  graph is recoverable). 1,874 of the classes are `Phyre::`, 137 are global scope
  (the FFX game code), the rest are Bullet / std / mkvparser.

## The big win: PhyreEngine keeps its reflection metadata in the binary

PhyreEngine registers every serializable class at runtime, with class names, sizes, and
**member names plus byte offsets**. That metadata is what the `.phyre` asset format
serializes against, so it has to be in the shipped exe. Reading it back out gives real
struct layouts with the original field names, rather than guessed ones.

The registration helpers (addresses in this build):

| Address | Name | Signature |
|---|---|---|
| `0x43B0A0` | `Phyre__PClassDescriptor__Init` | `__thiscall(this, PNamespace *ns, const char *name, u32 size, u32 align, PClassDescriptor *base, u32 flags)` - 1125 sites |
| `0x43AFB0` | `Phyre__PClassDescriptor__InitSimple` | `__thiscall(this, ns, name, size, align)` - 47 sites, data-less wrapper types |
| `0x5752A0` | `Phyre__PClassDescriptor__AddDataMember` | `__thiscall(memberStorage, PClassDescriptor *cls, PType *type, const char *name, u32 offset, u32 flags, u32)` - 751 sites |
| `0x43A080` | `Phyre__PClassMember__SetName` | `__thiscall(member, PClassDescriptor *cls, const char *name, u32 flags)` |
| `0x4395B0` | `Phyre__PClassDescriptor__EnsureInit` | guard at the head of each per-class descriptor getter |

`AddDataMember` is the one that matters. Each call site pushes, in order,
`flags, typeKind, offset, name, typeDesc, classDesc`, with `ecx` holding the static storage for
the member descriptor. Walking the pushes backwards from each of the 751 call sites recovers the
whole layout. Two gotchas:

- The compiler shares the pushed argument slots with an intervening call that produces the
  `typeDesc` (`push ...; push ...; call PType_getter; push eax; push classDesc; call AddDataMember`).
  Hex-Rays mis-assigns arguments on those sites, so parse at the disassembly level, not the
  pseudocode level. Anchor on the push whose value is a C string (the member name) and index
  relative to it.
- The primitive type getters each return a static `PType`, and the type's name is the first
  string literal in the getter body. Skip 1-character strings - there are stray `"N"` and `"U"`
  single-byte strings in those functions that will otherwise be picked up as the type name.

Class names come from the `??_7?$PClassDescriptor*<T>...` vtable symbols: the template argument
is the class, and the global that receives the vtable pointer is that class's descriptor. That
maps 418 descriptor globals to class names. A second, overlapping path is the `mov ecx, offset
<global>` at the `Init` call sites, which covers 334. Together they named all 259 descriptors
that actually carry data members.

### Reflected offsets are absolute and contain no vptr

The reflected data classes are non-polymorphic serialization PODs, so a recovered offset is a
plain byte offset from the start of the object. This cross-validates cleanly: `Phyre::PCamera`
has a declared size of 332, its last member sits at 316, and that member is a 16-byte vector,
so 316 + 16 = 332 exactly. Same for the math types - `PMatrix4x3` is 48 bytes and `PCamera`'s
`m_viewMatrix` runs 0..48, `Matrix4` is 64 and `m_projectionMatrix` runs 48..112.

When a member's resolved type is larger than its slot, it is a pointer. `Phyre::PTypedObject` is
8 bytes total with a `PClassDescriptor` (148 bytes) at offset 4, so that field is a
`PClassDescriptor *`. `phyre_types.h` applies that rule: a field gets its real type when the type
size equals the slot, becomes `Type *` when the slot is 4 bytes and the type is a class, and
otherwise falls back to a byte array with the intended type in a trailing comment. Gaps between
registered members become `_pad_<hex>` and unregistered trailing bytes become `_tail_<hex>` -
those are genuinely unknown private fields, not padding we chose.

## What is in this folder

- `phyre_types.h` - 259 recovered structs plus 27 opaque helpers and 4 math types. Applied to the
  IDB with zero parse errors; every size matches the reflection metadata. This is also directly
  usable from the mod's own C++ source for reading game memory.
- `phyre_reflection_meta.json` - raw extraction: class sizes, descriptor-global to class-name maps,
  and the per-class member lists with offsets, flags and resolved types.
- `ffx_types.h` - the **game-side** structs, not engine ones: `CHR` (0x880), `CHRDATA` (300),
  `CHRPART` (56), `CHRANIMF` (12). There is no reflection metadata for these, so they were
  recovered by reading the accessor block in the chr.c module. Applied to the IDB with zero parse
  errors and both big sizes pinned by the binary itself. A field named `m_<word>` is understood, a
  field named `m_f/m_i/m_w/m_b` plus its hex offset is a real field at a confirmed offset and size
  whose purpose is not known yet, and an `_unk_<hex>` run is just a stretch nothing has told us
  about.
- `extract_phyre_reflection.py` - regenerates the Phyre half of the above. Run it in IDA after a game patch
  rather than hand-fixing addresses.

## Naming applied to the IDB

Functions went from **2,851 named to 20,077**. Placeholders (`sub_*` / `nullsub_*` / `j_sub_*`) are
down to 29,011, i.e. 59.1% of 49,088. Counts below are read back out of the IDB by name pattern, not
tallied from the scripts, so they are what is actually in the database. They are substring matches,
which matters: 1,644 of the descriptor stubs carry an IDA dedup suffix (`_2`, `_3`, ...) because the
same class has several identical getter stubs, so an `endswith` count under-reports by a factor of
six. The categories below are a strict partition, first match wins, and they sum to 20,077.

| count | naming scheme |
|---|---|
| 8,234 | `<Class>__vf<NN>` - vtable slots |
| 4,370 | other: the `vc32rtf` CRT signature matches, the 258 reflected methods, the `__FUNCTION__` assert-string pass, and analysis-derived names |
| 1,975 | `<Class>__GetStaticClassDescriptor` |
| 1,019 | FFX game code: `FFX_*`, `Sg_*`, `MagicFile*`, and the PS2 libraries (`yi*`, `rcbg*`, `op_*`) |
| 686 | `<Class>__GetClassDescriptor` / `__ClassDescriptor_ctor` |
| 677 | `<Class>__GetClassName` |
| 2,018 | `FFX_AtelSys_<Lib>_<func>_<role>` - ATEL script syscall handlers |
| 417 | Lua 5.2 functions |
| 360 | `j_<target>` thunks |
| 199 | `<Module>__f<ADDR>` source-file attribution |
| 57 | Phyre engine functions named by behaviour |
| 32 | `Phyre__PType__*` getters |

108 names carry a `maybe_` prefix, meaning the name is a guess. Everything else is either exact or
mechanical. 1,062 functions carry a repeatable comment recording the evidence for the name, which is
usually more useful than the name itself.

### Exact - the name came out of the binary, not from inference

- **`__FUNCTION__` assert strings (772).** PhyreEngine's scripting headers store `__FILE__`,
  `__LINE__` and `__FUNCTION__` into a global context triple (`0xC96A98` / `0xC96A9C` / `0xC96AA0`)
  before asserting. The function-name global has 1,547 write sites covering 772 functions, every one
  agreeing on a single value. This is the single richest naming source in the binary, and it is the
  *only* such triple - a scan of every `mov <global>, offset "<source path>"` in `.text` found no
  others.
- **`<Class>__GetStaticClassDescriptor` (1,975)** - stubs of the form
  `mov eax, offset <descriptorGlobal>; retn`, where the global's class is known from its
  `PClassDescriptor<T>` vtable.
- **`<Class>__GetClassName` (677)** - stubs returning a class-name string literal.
- **Reflected engine methods (258)** - from the `PMethodCallerConcrete<Class, Ret, Args...>`
  vtables. The template argument list *is* the signature and the function pointer stored right
  after the vtable pointer is the real method, so these carry the original method name and its
  full prototype. **208 now have real applied prototypes**, which is what makes
  `PComponent__getEntity` decompile to `return this->m_entity;`.
- **The whole Lua 5.2 module (417)**, every function in all 31 source files, `0x94A3D0-0x968390`.
  This one is worth reading about separately in `LUA_MODULE.md`, because the technique used to do it
  generalises: **MSVC laid out each object file's functions alphabetically by symbol name**, statics
  included, so once a file's address range is known the real Lua symbol list maps onto it
  mechanically, and any body that disagrees with its predicted name tells you the boundary is wrong or
  a static was inlined. 19 of the 31 files matched on the first try with zero discrepancies. Library
  files follow a variant rule: `luaL_Reg`-referenced functions first in source order, then the statics
  alphabetically. Two facts from it that any interop has to respect: **`lua_Number` is `float`, not
  `double`**, and **`sizeof(TValue) == 8`**, value at `+0` and tag at `+4`.
- 16 functions uniquely referencing a `Class::method` log string (`FmodMusic__initData`,
  `rcdtRsdLoad__LoadPly`, ...), and 18 from self-naming `SKS:` log strings plus identifiable
  constant-string stubs.

### Mechanical - the class is right, the rest is positional

- The **8,234 vtable slots**. Slots owned by exactly one vtable are attributed directly. Slots
  shared by several vtables were resolved to their **declaring base class using the RTTI class
  hierarchy** (`??_R3`/`??_R2`/`??_R1`), which placed 2,991 inherited implementations correctly
  instead of misattributing them to a random derived class. The slot *index* carries no meaning
  beyond its position - rename as behaviour is identified.
- The **199 `<Module>__f<ADDR>`** names, from the original source filename a function references in
  an assert string (`mkvparser.cpp`, `FmodMusic.cpp`, `tklib.c`, `rcbg*.c`, ...). Module attribution
  only, no behaviour implied.

### What is left

29,011 functions are still `sub_*`. The mechanical sources are now exhausted - of the
remaining tiny constant-returning stubs, a sampled 493 of 650 have **zero xrefs** (dead template
instantiations the linker kept), so they are not worth naming. Further progress needs actual
analysis. The highest-yield remaining lead, per the FFX-specific pass, is mining the xrefs of the
embedded PS2 `.c` filenames and Virtuos' warning strings, which name functions like `Ch_SetRot` and
`opu_part_run` directly.

**Thunks need no attention at all.** IDA propagates a name to a function's `j_*` thunks the moment
the target is renamed. An earlier version of this file said to re-run a thunk pass after naming work.
That was wrong, and the numbers now show it clearly: placeholder thunks went from 1,290 to **11**
purely as a side effect of naming their targets, with no thunk pass run at any point.

## The data files are open too

`../tools/vbf.py` reads all three shipped archives, and the formats on top of it are decoded far
enough to read every one of the 71,979 assets. The single most useful discovery: **the original PS2
data ships intact** under `ffx_ps2/ffx/master/jppc/`, in parallel with the remastered Phyre assets,
and that is where the game-logic data lives.

| doc | tool | what it covers | state |
|---|---|---|---|
| `VBF_FORMAT.md` | `vbf.py` | the archive container | complete, header MD5 verified, 412 sampled files decompress exactly |
| `EBP_FORMAT.md` | `ebp.py` | the ATEL event scripts, container and full bytecode | 397/397 parse, 17.1M instructions decode, disassembler works |
| `CHR_FORMAT.md` | `chrfile.py` | the character containers | 865/865 parse, 0 errors |
| `MGRP_FORMAT.md` | `mgrp.py` | motion groups and the sequence bytecode | 3,156/3,156 parse, 0 rejections |
| `MAP_FORMAT.md` | `mapfile.py` | map containers and the walkmesh | 491/491 parse, exports OBJ |
| `FFX_GAME_NOTES.md` | `magicdll.py` | the shipped magic-DLL plugin system | ABI and host API table mapped |
| `../COOP_DESIGN.md` | `pcmodels.py` | which character models are spawnable, and the co-op design that needs them | 25 of 35 `c`-series models have field locomotion |

Each format doc marks every field as confirmed or inferred, and ends with explicit known unknowns and
what would settle each one. The tools do the same in their output rather than guessing.

A good cross-check that the formats are right: `chrfile.py` reads Tidus's idle, walk and run motion
ids out of `c001.chr` section 5, and `mgrp.py` independently reports the same ids out of
`resident0.mgrp`. Two tools, two files, same answer.

## Phase 1

`PHASE1_CLONE.md` is the research write-up for spawning a separately controllable second character,
and it is where the CHR work was aimed. The question it exists to answer is now settled: the
character data, motion and walkmesh systems were each audited for one-instance assumptions, and
**exactly one exists** in the whole path, a single dword cache at `g_ffxTidusChr 0x12FBC60` that one
assignment fixes.

See `FFX_GAME_NOTES.md` for the FFX-specific map: the CHR entity struct, the frame loop, the input
chain, the ATEL event script VM, the camera, the intact PS2 developer menu, the shipped magic-DLL
plugin system, the VBF asset path, and the save-data globals.

`VBF_FORMAT.md` is the decoded archive format. All 71,979 assets are readable with `../tools/vbf.py`,
including the **original PS2 data, which ships intact** under `ffx_ps2/ffx/master/<lang>pc/` and is
where the game-logic data lives (event scripts, character data, battle tables).

`LUA_MODULE.md` is the statically linked Lua 5.2 VM, all 31 source files located and all 417
functions named, plus the recovered struct layouts, enums and build configuration. Read its layout-law
section even if you do not care about Lua, because that technique applies to every statically linked
library in this binary.

A name prefixed `maybe_` is a guess. Everything in the Exact table above is not.
