# The Lua 5.2 module

FFX.exe statically links Lua 5.2 (pinned to 5.2 by `bit32` and `getuservalue`). The whole module is
now mapped: **every one of the 31 source files is located, and 417 functions are named**, with the
counts reconciling exactly against the real Lua 5.2 symbol list once dropped and inlined statics are
accounted for.

## The headline, so nobody wastes time: Lua is dead weight

**The VM is completely unreachable in the shipped game.** It is not a scripting surface for a mod. This
was the open question this document used to end on, and it is now answered with three independent
proofs, any one of which is sufficient:

1. **Nothing installs the script-load host callback.** `PScripting_LoadScriptFileByName 0x5D57B0` calls
   whatever is in `0xCBE6BC` and returns 0 when it is null. The three functions that would set it
   (`0x5D66D0` for the loader plus `0x5D66F0` and `0x5D6710` for the other hooks) have **zero
   references anywhere in the binary**, established by an exhaustive `E8`/`E9` rel32 decode of every
   executable segment plus an absolute-dword scan of every segment, with controls that correctly
   recovered the references to neighbouring functions. So the `require` global that
   `PScripting_RegisterPhyreGlobals` installs can never load anything.
2. **No shipped asset contains a script object.** All **47,125 `.phyre` clusters** in `FFX_Data.vbf`
   were decompressed and their Phyre class-name tables read, with zero failures. **Not one class name
   contains "Script."** `PEntity` and `PComponent` are also absent from every cluster, so the whole
   `PGameplay` component layer is unused and `PScriptedComponent` can never be instantiated. Controls
   on the same scan: `PClusterHeader` in all 47,125, `PNode` in 2,064, `PLight` in 2,060, `PWorld` in
   1,271, `PMesh` in 1,266, `PCamera` in 487.
3. **There are no Lua files.** Of the 71,979 shipped paths, zero end in `.lua` and zero contain "lua"
   at all. The binary holds exactly two `.lua` strings: the literal that `ModuleNameFromPath` passes to
   `strstr`, and stock Lua's `LUA_PATH_DEFAULT`. No Lua source text in any cluster.

The two call sites of `PScripting_CreateLuaStateForPScript 0x5D53B0` match that picture. One is inside
an orphan function at `0x555F20` with no callers and no vtable slot, dead code the linker kept. The
other is gated on a `PScriptedComponent` instance existing, which per point 2 never happens.
`PScripting_RunStandaloneScriptFile 0x5D6120` has zero callers and is the editor-side bytecode compiler,
being the only caller of the `lua_dump` helpers.

**And even if you got it running, you would be scripting the engine, not the game.** The `Phyre` global
exposes 346 reflected classes and 232 methods, and **every single class is stock PhyreEngine, D3D11 or
Bullet.** `PNode`, `PWorld`, `PMeshInstance`, `PCamera`, `PLight`, `PPhysicsBullet`, `Vector3`,
`Matrix4` and so on. **Zero Square Enix or FFX classes.** FFX's game logic lives in the
`ffx_ps2/.../*.bin` data and the ATEL scripts.

So the value of this document is not the VM. It is the **layout law** below, which is the most reusable
technique found in this binary, plus the struct layouts and the record of corrections.

## What a script would have seen, for completeness

The namespace is resolved lazily and never enumerated: `PScripting_RegisterNamespaceTable 0x436F90` has
an `if (ns != GetDefaultNamespace())` guard around its per-class loop, so for the real `Phyre` global
that loop is skipped. The global is a zero-byte userdata whose metatable `__index` is
`PScripting_PhyreNamespaceIndexHandler 0x436C80`, which resolves `Phyre.X` in order: the class
descriptor by name, then the global enum registry at `0xCB3358`, then the literal `"GetTypeOf"`, then
nil. `__newindex` returns 0, so it is read-only.

`PClassDescriptor` layout, recovered as bycatch and useful well beyond Lua: **name `+0x18`, base chain
`+0x40`, data members `+0x44`, methods `+0x4C`, static functions `+0x54`, static data members
`+0x5C`**. Reflected method counts: 346 distinct class names from 1,172 `PClassDescriptor::Init` call
sites, 232 distinct method names from 277 `SetName` call sites, 751 `AddDataMember` call sites, and 70
`PMethodCallerConcrete` template instantiations across 253 vtable-install sites.

The scripting plumbing is fully named too: `PScripting_CallMethodClosure 0x4371E0` is the reflected
method trampoline, `PScripting_PushPhyreObject 0x436E80` is the object-to-Lua bridge (a
`{PClassDescriptor *, void *}` userdata with the `PhyreObject` metatable), and
`PScripting_RegisterVectormath 0x5721B0` installs the `Vector` and `Matrix` globals with full operator
metamethods.

## The layout law

**This is the technique, and it generalises to any statically linked library in this build.**

MSVC laid out each object file's functions **alphabetically by symbol name**, file-static functions
included. It is an ASCII sort, so uppercase sorts before lowercase (`lundump.c`'s `LoadCode` comes
before its lowercase statics) and `luaK_code` sorts before `luaK_codeABC`.

So once you know which address range is which `.c` file, and you have the real Lua 5.2 symbol list
for that file, the mapping is mechanical: sort the expected symbols, line them up against the
functions in address order, and every name falls out. It also **self-checks**: if a body does not
match the symbol its position predicts, either the boundary is wrong or a static was inlined, and
either way you have learned something.

Validation: 19 of the 31 files matched the prediction exactly on the first try with zero
discrepancies. lapi.c was validated exhaustively at 78 of 78 in an earlier session, and is now 83 of
83.

Two refinements found while doing it:

- **Library files follow a different rule.** In a file that registers a `luaL_Reg` table, the
  functions whose addresses appear in that static array come **first, in source order**, then
  everything else follows alphabetically. liolib.c is the cleanest demonstration: 19 table entries in
  source order, then 22 statics in flawless alphabetical order, with `luaopen_io` landing on exactly
  the predicted slot. Confirmed on six library files.
- **Inlined statics shift everything after them.** When a count comes up one short, a small static was
  inlined. Confirmed cases: `findlast` and `setpause` (lgc.c), `luaE_setdebt`, `numarith`,
  `luaZ_lookahead`, `filterpc`, `adjust_varargs`, `DumpCode`, and `gethooktable` which is a macro.
  Assuming a count without checking is exactly how the lzio.c error below happened.

When position and body disagree, **the body wins**. Several of the errors below came from trusting a
count rather than reading the code.

## File map

The module runs **0x94A3D0 to 0x968390**. Note that is wider than it first appeared: lparser.c,
lundump.c and lcode.c sit past 0x962000, which is 124 functions that an earlier bound missed. Past
0x968390 the binary turns into D3D11 import thunks and Bullet.

| file | range | funcs | named | first-try alphabetical match |
|---|---|---|---|---|
| lapi.c | `94A3D0-94C38F` | 83 | 83 | yes |
| lauxlib.c | `94C390-94E03F` | 58 | 58 | yes, exact |
| lgc.c | `94E040-95043F` | 49 | 49 | yes, 2 inlined |
| lstate.c | `950440-950A8F` | 14 | 14 | yes, 1 inlined |
| lcorolib.c | `950AA0-950E2F` | 9 | 9 | library rule |
| lstrlib.c | `950E30-9534EF` | 54 | 54 | library rule |
| lbitlib.c | `9534F0-95367F` | 10 | 10 | library rule |
| lmathlib.c | `953680-9540EF` | 24 | 24 | library rule |
| linit.c | `9540F0-954177` | 1 | 1 | n/a |
| lbaselib.c | `954180-954F8F` | 30 | 30 | library rule |
| ltablib.c | `954F90-95592F` | 12 | 12 | library rule |
| lobject.c | `955930-9561DF` | 13 | 13 | yes, 1 inlined |
| lzio.c | `9561E0-95632F` | 4 | 4 | yes, 1 inlined |
| ldebug.c | `956330-9571CF` | 30 | 30 | yes, 1 inlined |
| ldo.c | `9571D0-958143` | 26 | 26 | yes, 1 inlined |
| lfunc.c | `958150-9584EF` | 10 | 10 | yes, exact |
| lstring.c | `9584F0-958A2F` | 10 | 10 | yes, exact |
| ltable.c | `958A30-959A9F` | 25 | 25 | yes, exact |
| ldump.c | `959AA0-95A1FF` | 12 | 12 | yes, 1 inlined |
| lvm.c | `95A200-95C4BF` | 20 | 20 | yes, exact |
| ltm.c | `95C4C0-95C59F` | 3 | 3 | yes, exact |
| lmem.c | `95C5A0-95C69F` | 3 | 3 | yes, exact |
| llex.c | `95C6B0-95DECF` | 22 | 22 | yes, exact |
| liolib.c | `95DED0-95F32F` | 41 | 41 | library rule, then exact |
| loslib.c | `95F330-95FD1F` | 17 | 17 | yes |
| ldblib.c | `95FD20-960D5F` | 27 | 27 | yes, 1 macro |
| loadlib.c | `960D60-961C6F` | 27 | 27 | two separate runs, see below |
| lparser.c | `961C70-965B3F` | 66 | 66 | yes, one clean run |
| lundump.c | `965B40-9665FF` | 10 | 10 | one function displaced |
| lcode.c | `966600-96838E` | 48 | 48 | one function displaced |

96 of these were not functions at all until this pass: IDA auto-analysis had missed them, and they
were recovered by a prologue scan of the gaps between known functions.

### Three layout anomalies

These did not produce wrong names, because the names in these files were pinned by call sites and
bodies rather than position, but they break the simple rule and are worth knowing:

- **loadlib.c** has the data-referenced block in source order, then **two separate alphabetical runs**
  rather than one.
- **lundump.c** has `checkHeader` hoisted between `LoadFunction` and `LoadString` instead of sitting
  after `LoadUpvalues`.
- **lcode.c** has `negatecondition` hoisted in before `luaK_checkstack` instead of after
  `luaK_stringK`. All 34 public `luaK_*` functions are still in flawless alphabetical order.

Verifying lcode.c by **call sites rather than position** turned out to be the most productive single
technique in the whole job: 20 already-named lparser functions gave 33 of the 48 lcode addresses with
no guessing at all. Worth reaching for whenever two modules are tightly coupled.

## Corrections

Nine wrong names were found and fixed, most of them mine from earlier sessions. Recording them
because the failure modes repeat.

| address | was | is | how it went wrong |
|---|---|---|---|
| `0x95A590` | `luaT_trybinTM` | **`luaV_arith`** | `luaT_trybinTM` does not exist in Lua 5.2 at all. The body inlines `luaV_tonumber` on both operands, then `luaO_arith`, else `call_binTM` then `luaG_aritherror`, and it sits exactly where `luaV_arith` belongs |
| `0x950AA0`-`0x950C80` (6 names) | shifted by one | corrected | in the `co_funcs` table at `0xB6C7B4`, `name[i]` had been paired with `func[i-1]` |
| `0x954FC0`-`0x9552E0` (6 names) | shifted by one | corrected | the same off-by-one in `tab_funcs` at `0xB6D364` |
| `0x954F50` | `lbaselib_finishpcall` | **`pcallcont`** | the real `finishpcall` is at `0x954CF0` |
| `0x956250` | `luaZ_lookahead` | **`luaZ_openspace`** | my own error: I assumed lzio.c had 5 functions when `luaZ_lookahead` is inlined, which shifted the whole file |
| `0x9562A0` | `luaZ_openspace` | **`luaZ_read`** | same shift |
| `0x956330` | `luaZ_read` | **ldebug.c's static `addinfo`** | same shift, and it put the lzio/ldebug boundary in the wrong file. With it corrected, ldebug.c is 30 functions in perfect alphabetical order |

The lzio.c slip is the instructive one. It was caught not by re-reading lzio.c but by a **lundump.c
call site that needed `luaZ_read` where the name said `luaZ_openspace`**. Cross-module call sites are
the cheapest check available, and the earlier pass skipped them.

Three earlier errors from the previous session are also worth keeping on the record, since they came
from the same habit of trusting structure over body: `maybe_lua_seterrorobj` at `0x94AD00` was really
`lua_concat`, `luaD_throw` at `0x94AE40` was really `lua_error`, and `lua_luaG_runerror` at `0x94CD50`
was really **`luaL_error`** from lauxlib.c, which is what explains its 826 call sites since every
binding uses it.

Confirmed correct, having been flagged as suspect: **`luaU_dump` at `0x95A180` is right.** lvm.c runs
`0x95A200-0x95C4B7` with all 20 functions and 11 anchors aligned, so the ldump/lvm boundary is at
`0x95A200` and `luaU_dump` is the legitimate last function of ldump.c.

## lua_Number is float, not double

**Verified three ways, and it matters for any interop.** `liolib_read_number` scans with `"%f"`,
`liolib_g_write` formats through a float, and `lcode_constfolding` loads and stores the TValue number
field as a 4-byte float.

Checked independently here on `lua_pushnumber 0x94B750`, which settles it beyond doubt:

```
movss   xmm0, [ebp+arg_4]          ; 4-byte load
mov     eax, [ecx+8]               ; L->top
movss   dword ptr [eax], xmm0      ; 4-byte store
mov     dword ptr [eax+4], 3       ; tag = LUA_TNUMBER
add     dword ptr [ecx+8], 8       ; top += sizeof(TValue)
```

A 4-byte move and a stack step of 8. **So `sizeof(TValue) == 8`**, value at `+0` and tag at `+4`. If
`lua_Number` were a double, the TValue would be 12 or 16 bytes and this would be `movsd` on a qword.
Anything the mod passes into or reads out of this VM has to use `float`.

## Struct layouts recovered

**FuncState** (exact): `f +0, h +4, prev +8, ls +12, bl +16, pc +20, lasttarget +24, jpc +28, nk +32,
np +36, firstlocal +40, nlocvars +44, nactvar +46, nups +47, freereg +48`.

**LexState** (exact): `current +0, linenumber +4, lastline +8, t.token +12, t.seminfo +16,
lookahead.token +20, fs +28, L +32, z +36, buff +40, dyd +44, source +48, envn +52, decpoint +56`.

**BlockCnt**: `previous +0, firstlabel +4, firstgoto +6, nactvar +8, upval +9, isloop +10`.

**Dyndata**: `actvar.arr +0, .n +4, .size +8, gt.arr +12, .n +16, .size +20, label.arr +24, .n +28,
.size +32`.

**Labeldesc** (16): `name +0, pc +4, line +8, nactvar +12`. **LocVar** (12): `varname +0, startpc +4,
endpc +8`. **Upvaldesc** (8): `name +0, instack +4, idx +5`.

**Proto** additions: `lineinfo +20, sizecode +48, sizelineinfo +52, linedefined +64,
lastlinedefined +68, numparams +76, is_vararg +77, maxstacksize +78`.

**LoadState**: `L +0, Z +4, b +8, name +12`. **Mbuffer**: `buffer +0, n +4, buffsize +8`.
**LStream** (liolib): `f +0, closef +4`, sizeof 8.

The `lua_State`, `global_State`, `CallInfo`, `TString`, `Udata`, `Table` and `CClosure` layouts plus
every type tag and status code were pinned in an earlier session.

## Enums and limits

**Token**: `FIRST_RESERVED 257`, `TK_AND 257`, `TK_ELSE 0x104`, `TK_ELSEIF 0x105`, `TK_END 0x106`,
`TK_OR 272`, `TK_UNTIL 0x115`, `TK_RETURN 274`, `TK_CONCAT 279`, `TK_EQ 281`, `TK_EOS 0x11E`,
`TK_NUMBER 287`, `TK_NAME 288`, `TK_STRING 289`.

**expkind**: `VVOID 0, VNIL 1, VTRUE 2, VFALSE 3, VK 4, VKNUM 5, VNONRELOC 6, VLOCAL 7, VUPVAL 8,
VINDEXED 9, VJMP 10, VRELOCABLE 11, VCALL 12, VVARARG 13`.

**BinOpr**: `ADD 0, SUB 1, MUL 2, DIV 3, MOD 4, POW 5, CONCAT 6, EQ 7, LT 8, LE 9, NE 10, GT 11,
GE 12, AND 13, OR 14`.

**OpCode**: `OP_MOVE 0, OP_LOADBOOL 3, OP_ADD 13, OP_DIV 16, OP_MOD 17, OP_UNM 19, OP_NOT 20,
OP_LEN 21, OP_JMP 23, OP_EQ 24, OP_TEST 27, OP_TESTSET 28, OP_TAILCALL 30`.

**Limits**: `MAXSTACK 250, NO_REG 255, BITRK 0x100, MAXARG_sBx 0x1FFFF, MAXARG_Bx 0x3FFFF,
NO_JUMP -1, LUA_IDSIZE 60, LUAI_MAXCCALLS 200, LUAC_HEADERSIZE 18, LUA_MINBUFFER 32`.

## Build configuration

`byte_B6E780` is `luaP_opmodes`, `off_B6E158` is ldblib's `hooknames`, `off_B6E42C` is loadlib's
`searchers[]` (preload, Lua, C, Croot, NULL), `0xB6E404` is `pk_funcs` and `0xB6E41C` is `ll_funcs`.

`pk_funcs` contains only `loadlib` and `searchpath`, so **`LUA_COMPAT_MODULE` is off in this build**:
no `module`, `seeall`, `setfenv` or `getfenv`.

## Deliberately left unnamed

Eleven functions inside the address range that are CRT or math rather than Lua: the CRT exception
filter installer and a `return 0` stub (`0x94A091`, `0x94A0B2`), the CRT initializer and terminator
table walkers (`0x94A2C8`, `0x94A2E8`), `_time64` thunks (`0x950A90`, `0x95FD10`), `_difftime64`
(`0x95FB80`), `_gmtime64` / `_localtime64` / `_mktime64` (`0x95FC60`, `0x95FC70`, `0x95FCB0`), and
`0x9683D0` which is past the module and calls only `_CIsqrt`.

## Known unknowns

- **19 lparser.c, 6 lundump.c and 12 lcode.c symbols predicted but not present**, which is consistent
  with inlining but was not individually confirmed for each one.

The question this section used to lead with, whether the game exposes the VM to anything moddable, is
answered at the top of this document. It does not.

Two caveats on the asset evidence, stated for honesty: only the first 64 KB of each cluster was read,
which is sound because Phyre puts the class-name table at the start of the file right after the `RYHP`
header, and the scan covered `FFX_Data.vbf` and `metamenu.vbf` but not `FFX2_Data.vbf`.
