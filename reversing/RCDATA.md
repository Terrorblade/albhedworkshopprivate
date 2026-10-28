# rcData, the PS2 data library

PROGRESS 1.7.7 in projectzanarkand. What the four `rcData` object files are, the formats they read,
who calls them in retail, and whether the `.rsd`, `.ply`, `.ma2` and `.tm2` files that ship in
`FFX_Data.vbf` are live. All addresses are IDA VAs (RVA + 0x400000). Evidence tags as elsewhere: [V]
read in the binary, [M] measured by a scan over shipped data, [I] inferred. [V] and [M] are the proved
ones.

Proposed names are in `tools/apply_rcdata_names.py` (77 rows). The doc uses them and gives the old
IDB name the first time when it differs.

## 1. Summary

- **rcData is two small utilities and two text era loaders.** `rcdtChunk` is a `{data, size}`
  buffer that can be filled from a file. `rcdtParse` is a cursor over a chunk that reads either raw
  binary or whitespace separated text tokens. `rcdtRsd2` loads the PlayStation 1 style RSD model set
  (`.rsd` manifest, `.ply` geometry, `.mat` or `.ma2` materials). `rcdtTm2` loads one TIM2 picture.
  [V]
- **Only the binary half of chunk and cursor is live in retail.** The rcBg map stream walker
  (`rcbgMain__f921D60`, reached from map load through `FFX_RcBg_LoadYnStream`) wraps each in memory
  section with `rcdtChunkSetMemory` and reads every field through `rcdtParseRead` and
  `rcdtParseSkip`. No file is opened and no text is parsed on that path. Section 5. [V]
- **The text and file side is dead.** `rcdtRsdLoad`, `rcdtTm2Load` and `rcdtChunkLoad` are reached
  only from the rcBg text loader, the rcUI tool and two rcbgDebug loaders, and every path up from them ends at a function
  with no reference of any kind, or at `SG_DebugGui_RootWindowProc`, whose only reference is the
  orphan opener at 0x84F100 that `DEBUG_MODE.md` section 4 already proved unreachable. Section 5. [V]
- **The files ship anyway, as tool sources.** 230 `.rsd`, 230 `.ply`, 230 `.ma2`, 18 `.grp`, 2
  `.vgr` and 84 `.tm2`, all under `ffx_ps2/ffx/yonishi_data/` (the effect tool tree) except 10 `.tm2`
  under `eiichi_abmap_data/maho/tim/`. They are exactly the format the dead parsers read, every one of
  them parses cleanly under the parser's own rules except two 16 bit `.tm2` the TIM2 loader would
  reject, and no string in the exe names any of them.
  Section 6. [M]
- **Verdict: `.rsd`, `.ply`, `.mat`, `.ma2`, `.grp`, `.vgr` and `.tm2` are dead in retail** because no
  retail path reaches a parser, with the files present. The paths the dead loaders build
  (`host:/ffx/proj/map/master/...`, `host:%s/2d/tex/...`) also name files that do not ship. Proposed
  PORTING_RULES H rows in section 8. [V] [M]

## 2. Code map

The source strings sit at 0xB678F8 (`..\program\src\rcData\rcdtTm2.c`), 0xB6793C (`rcdtRsd2.c`),
0xB67C8C (`rcdtChunk.c`) and 0xB67CB0 (`rcdtParse.c`). The four files are not adjacent. Tm2 and Rsd2
sit between `rcbgFP.c` and the rcBase/rcbgModel/rcFlex block, Chunk and Parse right after
`rcbgLoad.c`, so the link order of the rc libraries is not alphabetical by file. Inside each file the
alphabetical layout law (LUA_MODULE.md) holds where it can be checked, statics first because an
uppercase static sorts before the lowercase `rcdt` prefix. [V]

| file | range | functions |
| --- | --- | --- |
| rcdtTm2.c | 0x923240..0x923B5E | statics `rcdtTm2_ConvertClutAlpha` 0x923240, `_GetPsm` 0x923280, `_LoadFile` 0x9232A0 (`rcdtTm2__f9232A0`). Delete 0x923450, Final 0x923470, GetAlphaRect 0x9234D0, GetBlackRect 0x923660, Init 0x9237E0, Load 0x923800, UploadImage 0x923820, LoadNoAlpha 0x923960, ReturnInvalid 0x923980, UploadPixels 0x923990, UploadRect 0x923A10, CopyRect 0x923AE0, New 0x923B40 (`rcdtTm2__f923B40`) |
| rcdtRsd2.c | 0x923B60..0x92476E | statics `rcdtRsdLoad__LoadMat` 0x923B60, `rcdtRsdLoad__LoadMatRsd` 0x923BC0, `rcdtRsdLoad__LoadMatRsd2` 0x923ED0, `rcdtRsdLoad__LoadPly` 0x924090 (the last three named from their own messages). Delete 0x9242D0, Final 0x9242F0, Init 0x924390, Load 0x9243C0 (`rcdtRsd2__f9243C0`, exact name `rcdtRsdLoad`), New 0x924750 (`rcdtRsd2__f924750`) |
| rcdtChunk.c | 0x92B7D0..0x92B954 | Delete 0x92B7D0, Final 0x92B7F0, GetData 0x92B810 (`unknown_libname_656`), GetEnd 0x92B820, GetSize 0x92B830, Init 0x92B840, IsEmpty 0x92B860, Load 0x92B880 (`rcdtChunk__f92B880`), New 0x92B900 (`rcdtChunk__f92B900`), Set 0x92B920, SetMemory 0x92B940 |
| rcdtParse.c | 0x92B960..0x92C125 | 44 functions, Skip 0x92B960 through AddDelimiters 0x92C0F0, listed in section 4.4 |

The checks that hold:

- rcdtRsd2: the four statics sort `LoadMat < LoadMatRsd < LoadMatRsd2 < LoadPly`, then
  `Delete < Final < Init < Load < New`. The public prefix is `rcdtRsd`, not `rcdtRsd2`, which the
  exact name `rcdtRsdLoad` (0xB67978, "rcdtRsdLoad : File not found.", read only at 0x924490) pins.
  The messages "rcdtRsdLoad::LoadPly", "::LoadMatRsd" and "::LoadMatRsd2" name the statics. [V]
- rcdtChunk: Delete, Final, GetData, GetEnd, GetSize, Init, IsEmpty, Load, New, Set, SetMemory is
  strictly alphabetical and every body matches its name. [V for the bodies, I for the names]
- rcdtTm2: three statics, then Delete, Final, two getters, Init, Load, and New last. The six
  orphans between Load and New must have real names that sort there. Mine are descriptive and do not
  claim that. [I]
- rcdtParse: Delete 0x92BA50, Final 0x92BA60, Init 0x92BF50, New 0x92BFD0 are in order. The names
  I gave the other 40 are descriptive and not chosen to sort. The six functions before Delete
  (0x92B960..0x92BA30) must have real names that sort before `rcdtParseDelete`. [I]

The file edges: 0x923190, right before rcdtTm2, walks a MapFx group like
`FFX_RcBgFP_GetGroupOpWork` and belongs to `rcbgFP.c`. 0x924770, right after rcdtRsd2, works on the
matrix scratch globals at 0xC8F970. 0x92B780 before rcdtChunk reads two rcBg trx images. 0x92C130
after rcdtParse works on the rcBg context `dword_193C0F0` and sits in the `rcbgDebug.c` range. [V]

Twenty rcdt functions are code that IDA had not made a function, because nothing references them:
0x923820, 0x923960, 0x923990, 0x923A10, 0x92B830, 0x92B9D0, 0x92BAC0, 0x92BB30, 0x92BB50, 0x92BC00,
0x92BC90, 0x92BCD0, 0x92BD10, 0x92BDA0, 0x92BDB0, 0x92BEE0, 0x92BF00, 0x92BF80, 0x92C060, 0x92C080.
Each starts after an `align 10h` and ends in `retn`. A sweep of every E8/E9 rel32 and every absolute
dword in the image finds no reference to any of them, nor to the two orphans IDA did have as
functions, `rcdtTm2ReturnInvalid` 0x923980 and `rcdtParseGetChunk` 0x92BB20. [V]

All allocation goes through `sub_9258A0`, the same heap wrapper rcFlex uses, and all frees through
`j_j_FFX_MemFreeLocked_2`. [V]

## 3. Who uses rcData

| caller group | uses | live |
| --- | --- | --- |
| `rcbgMain__f921D60` and `rcbgLoad.c` (0x92B1A0..0x92B780), the binary rcBg map stream | ChunkSetMemory, ParseInit, ParseSetChunk, ParseRead, ParseSkip, ParseFinal, ChunkGetData via SetChunk, ChunkDelete/Final via `sub_921AA0` | yes |
| `rcbgLoadText.c`, the text rcBg loader | RsdNew/Load/Delete, Tm2New/Load/Delete/GetAlphaRect/GetBlackRect/CopyRect | no |
| the rcUI tool (`rcuiPre__f930D80` and its callbacks), `FFX_RcDev_ReadMapNameCsv` 0x937960, `rcbgSave.c` | ChunkNew/Load, the text cursor (NextLine, GetInt, GetKeyword, AddDelimiters, IsEnd) | no |
| `rcbgDebug.c` loaders 0x92CCF0 ("DEBUG FP LOAD", `m%03d.fpd`) and 0x92CDD0 ("DEBUG GUIDE MAP LOAD", `gmobjdata.bin`) | ChunkNew/Load, then rcbgMain | no |
| 0x919AD0 | Tm2Init/Final, RsdInit/Final, `rcbgPSimInit` and six other calls, on stack objects, with no use of the results | no, it has no reference. It looks like a function kept only to pull objects into the link [I] |

The rcbg side of this table is the other agent's (`RCBG.md`). I name only the rcdt functions.

## 4. Formats and objects

### 4.1 TIM2, as rcdtTm2 reads it [V]

`rcdtTm2_LoadFile 0x9232A0 (obj, path, convertAlpha)`, wrapped by `rcdtTm2Load 0x923800` (alpha on)
and the orphan `rcdtTm2LoadNoAlpha 0x923960` (alpha off).

File, as read:

| off | size | field | read |
| --- | --- | --- | --- |
| 0x00 | 4 | magic `TIM2` | copied to obj +0, **never compared** |
| 0x04 | 1, 1 | version, format (alignment) | copied, not checked. The loader assumes the picture header at 0x10, which is format 0 |
| 0x06 | 2 | picture count | copied, only the first picture is read |
| 0x08 | 8 | reserved | copied |
| 0x10 | 4 | picture total size | copied, not used |
| 0x14 | 4 | clut size | obj +20, the clut read size |
| 0x18 | 4 | image size | obj +24, the clut seek |
| 0x1C | 2 | header size | obj +28, pixels start at 0x10 + this |
| 0x1E | 2 | clut colours | obj +30, read only by the orphan upload (256 or 16) |
| 0x20..0x22 | 1 each | picture format, mipmaps, clut type | copied, not used |
| 0x23 | 1 | image type | must be 4 (4bpp, w*h/2 bytes) or 5 (8bpp, w*h bytes), else "Image Type Error" and return 0 (compare at 0x923343) |
| 0x24, 0x26 | 2, 2 | width, height | obj +36, +38 |
| 0x28..0x3F | | GsTex0, GsTex1, GsRegs, GsTexClut | copied, not used |
| 0x10 + hdr | w*h or w*h/2 | pixels | heap buffer, handle at obj +64 |
| 0x10 + hdr + imageSize | clutSize | clut | heap buffer, handle at obj +68 |

Object, 0x50 bytes: +0 the 16 byte file header, +16 the 0x30 byte picture header, +64 pixels and
+68 clut (both `maybe_FFX_PrxPtr_Set` handles), +72 the GS PSM from `g_rcdtTm2PsmByImageType`
0xC5E754 (6 dwords, type 1..5 -> 2, 1, 0, 0x14, 0x13), +76 no reader found.

`rcdtTm2_ConvertClutAlpha 0x923240` walks **256** clut entries whatever the clut size: alpha 0
zeroes the entry, any other alpha becomes `a*128/255` (multiply by `0x80808081`, shift 39). On a 16
colour clut that is a 1,024 byte walk over a 64 byte buffer. One shipped file (`num_16.tm2`) would
hit it if anything loaded it.

`rcdtTm2GetAlphaRect 0x9234D0` and `rcdtTm2GetBlackRect 0x923660` scan 8bpp pixels for clut entries
with nonzero alpha, or with rgb exactly 0, and return the bounding rect (the alpha one rounds x down
and w up to even). The black one is used on rcbg's `%spos.tm2` images, where black marks positions
[I from the path]. `rcdtTm2CopyRect 0x923AE0` copies a pixel sub rect with an 8 byte row stride. The
three upload orphans push pixels and clut through `maybe_FFX_Gs_LoadImage425_Stub` and `_423_Stub`,
which return 0 on PC, so the pixels could never reach a GPU even if called.

### 4.2 The RSD model set [V]

These are the PS1 era RSDTOOL formats (MatEditor wrote the shipped ones), all ASCII. The parser runs
the cursor in text mode: tokens are separated by bytes 0x00..0x20 plus whatever
`rcdtParseAddDelimiters` adds, numbers are `atoi` / `atof` on the token.

**`.rsd`, the manifest, `rcdtRsdLoad 0x9243C0 (rsd, path)`.** Lines starting `@` or `#` are skipped
with NextLine. Delimiters gain `[]=`. Each line starts with a keyword matched case insensitively by
`rcdtParseGetKeyword 0x92BE40` against the table built on the stack at 0x9243E7..0x924419:

| index | keyword | action |
| --- | --- | --- |
| 0 | `PLY` | read the name, lowercase it, load `<dir of .rsd>/<name>` with `rcdtRsdLoad__LoadPly` |
| 1 | `MAT` | same, through `rcdtRsdLoad__LoadMat`, which takes extension `mat` -> LoadMatRsd and `ma2` -> LoadMatRsd2 and ignores anything else |
| 2 | `GRP` | read the name, lowercase it, drop it |
| 3 | `NTEX` | n = GetInt, if n > 0 allocate 4n bytes at rsd +28. The count is never stored at +24 (0x92465A..0x924684 writes only `[edi+1Ch]`), so `rcdtRsdFinal` frees none of the names |
| 4 | `TEX` | `TEX[i]=name`: i = GetInt, allocate the token length + 1 at `rsd+28[i]` and copy the name. Nothing loads the texture |
| 5 | `VGR` | read the name, drop it |

RSD object, 32 bytes: +0 vertex count, +4 vertices (vec4, w = 1), +8 normal count, +12 normals
(vec4, w = 1), +16 polygon count, +20 polygons (88 bytes each), +24 texture count (never written),
+28 texture name pointers.

**`.ply`, `rcdtRsdLoad__LoadPly 0x924090`.** Skip the `@` and `#` lines, read `nVert nNorm nPoly`,
then NextLine once, nVert lines of `x y z`, NextLine, nNorm lines of `x y z`, NextLine, nPoly lines
of `type v0 v1 v2 v3 n0 n1 n2 n3`. Because the token reader also skips newlines, each NextLine
actually steps over one comment line (`# Vertex`, `# Normal`, `# Polygon`). A file without exactly
one line between sections would lose its first data line. type != 0 sets polygon flag bit 0 (a
quad). Indices are stored as u16 widened to dwords.

Polygon record, 88 bytes:

| off | what | written by |
| --- | --- | --- |
| +0 | flags: bit 0 quad, bit 1 textured | PLY, MAT types D H T |
| +4 | vertex index x4 | PLY |
| +20 | normal index x4 | PLY |
| +36 | texture index, -1 for none | MAT D H T, MA2 |
| +40 | (u, v) x4 as dword pairs | MAT D H T, MA2 H |
| +72 | rgba x4 bytes | MAT, MA2 |

**`.mat`, `rcdtRsdLoad__LoadMatRsd 0x923BC0`.** Count first, asserted against the polygon count
("cnt == self->m_cntPolygon", line 327, the assert is a no-op print). Then per polygon: three ints
that are read and dropped (polygon, flag, and the F/G shading letter which `atoi` turns into 0),
then the type letter as one raw byte, then:

| type | reads | writes |
| --- | --- | --- |
| C | r g b | the colour to all four vertices, alpha 0x80 |
| G | r g b x4 | four colours, alpha 0x80 |
| T | tex, (u v) x4 | flag bit 1, tex, uv, all colours 0x80808080 |
| D | tex, (u v) x4, r g b | flag bit 1, tex, uv, one colour to all four |
| H | tex, (u v) x4, (r g b) x4 | flag bit 1, tex, uv, four colours |

The polygon index on each line is ignored, records fill in file order.

**`.ma2`, `rcdtRsdLoad__LoadMatRsd2 0x923ED0`.** Header `@MAT990928` in all shipped files. Count,
asserted the same way (line 461). Per polygon: eight ints read and dropped (the index, then six
fields, then the shading letter), the type letter, then if `H` the texture index and four (u, v)
pairs, otherwise texture = -1, then four `r g b a` quads. Unlike LoadMatRsd it **never sets flag bit
1**, so a polygon loaded from a `.ma2` is not marked textured. [V, the only flag write in the
function would be an `or`, and there is none]

`.grp` and `.vgr` have no parser. `rcdtRsdLoad` reads their names and drops them. Their shipped
headers are `@GRP940102` and `@VGR940102`. [V for no parser, M for the headers]

### 4.3 rcdtChunk [V]

8 bytes: +0 data, +4 size. `rcdtChunkLoad 0x92B880 (chunk, path)` calls Final, opens the path with
`FFX_File_OpenHost0 0x62FA30` mode 1, takes the size by seeking to the end, reads the whole file
into a heap buffer and sets it. It returns 1, or 0 when the open fails. `FFX_File_OpenHost0` drops
the first 7 bytes of the path and prepends `/ffx_ps2/`, so it is written for `host0:/...` paths and
maps them into the archive (its IDB comment from CHEAT_WARP). The rcbg text paths start `host:`,
5 bytes, so a literal like `host:/ffx/proj/map/master/...` would become `/ffx_ps2/fx/proj/...`. [V
for the strip, I that every text path has that shape]

`rcdtChunkSetMemory 0x92B940 (chunk, data)` sets size 0x7FFFFFFF. That is the live entry: rcbgMain
wraps each in memory section of the YN stream in a stack chunk this way, so the end checks never
fire on the live path.

### 4.4 rcdtParse [V]

0x8C bytes: +0 the chunk, +4 the cursor, +8 text mode (0 binary, 1 text), +12 a 128 byte delimiter
table. `rcdtParseInit 0x92BF50` zeroes the first three and calls `rcdtParseResetDelimiters 0x92C000`,
which clears the table and marks bytes 0x00..0x20. The table is indexed by the unsigned byte with no
bound, so a text byte >= 0x80 reads up to 127 bytes past the object. Only text mode looks at the
table.

The value readers branch on +8. In text mode `GetInt`/`GetLong`/`GetFloat` run `atoi`/`atol`/`atof`
on the token and SkipToken. In binary mode they read 4 raw little endian bytes. The `Swap` readers
are binary only and reverse the bytes, for big endian data. Only binary mode is ever used live.

| ea | name | what |
| --- | --- | --- |
| 0x92B960 | rcdtParseSkip | cursor += n. LIVE |
| 0x92B970 | rcdtParseSkipLine | past the next newline |
| 0x92B990 | rcdtParseSkipTo | to the next given byte |
| 0x92B9B0 | rcdtParseNextLine | SkipLine, then the delimiters |
| 0x92B9D0 | rcdtParseSkipBlock | orphan. Over a nested `()`, `{}` or `[]` group |
| 0x92BA30 | rcdtParseSkipToken | to the token end, then the delimiters |
| 0x92BA50 | rcdtParseDelete | was `j_j_j_FFX_MemFreeLocked_2_6`, a plain free |
| 0x92BA60 | rcdtParseFinal | was `nullsub_173`, empty. LIVE |
| 0x92BA70 | rcdtParseGetBase | chunk data |
| 0x92BA90 | rcdtParseRead | memcpy n bytes, cursor += n. LIVE |
| 0x92BAC0 | rcdtParseReadChar | orphan |
| 0x92BAE0 | rcdtParseFindChar | pointer to the next given byte |
| 0x92BB20 | rcdtParseGetChunk | was `unknown_libname_657`, orphan |
| 0x92BB30 | rcdtParseReadInt32 | orphan |
| 0x92BB50 | rcdtParseReadInt32Swap | orphan |
| 0x92BB90 | rcdtParseGetEnd | chunk end |
| 0x92BBB0 | rcdtParseGetFloat | text atof or raw float |
| 0x92BC00 | rcdtParseReadFloatSwap | orphan |
| 0x92BC50 | rcdtParseGetInt | text atoi or raw int |
| 0x92BC90 | rcdtParseGetLong | orphan, atol |
| 0x92BCD0 | rcdtParseReadUInt32Swap | orphan, the same bytes as 0x92BB50 |
| 0x92BD10 | rcdtParseGetMode | orphan |
| 0x92BD20 | rcdtParseTokenEnd | first delimiter |
| 0x92BD60 | rcdtParseSkipDelimiters | past the delimiters |
| 0x92BDA0 | rcdtParseGetCursor | orphan |
| 0x92BDB0 | rcdtParseGetOffset | orphan |
| 0x92BDD0 | rcdtParseGetString | copy the token out |
| 0x92BE10 | rcdtParseGetTokenLength | 0 in binary mode |
| 0x92BE40 | rcdtParseGetKeyword | index in a NULL ended table, or -1 |
| 0x92BEE0 | rcdtParseReadInt16 | orphan |
| 0x92BF00 | rcdtParseReadInt16Swap | orphan |
| 0x92BF30 | rcdtParseTokenLength | |
| 0x92BF50 | rcdtParseInit | LIVE |
| 0x92BF80 | rcdtParseIsEmpty | orphan |
| 0x92BFB0 | rcdtParseIsEnd | |
| 0x92BFD0 | rcdtParseNew | |
| 0x92C000 | rcdtParseResetDelimiters | LIVE through Init |
| 0x92C060 | rcdtParsePeekFloat | orphan |
| 0x92C080 | rcdtParsePeekInt | orphan |
| 0x92C0A0 | rcdtParseSetChunk | LIVE |
| 0x92C0C0 | rcdtParseSetText | only dead callers pass 1 |
| 0x92C0D0 | rcdtParseSeek | |
| 0x92C0F0 | rcdtParseAddDelimiters | |

## 5. Liveness

Method: from each loader, walk every code and offset cross reference upward to a fixed point, in a
session where the undefined code in 0x918000..0x940000 was made into functions first (142 of them,
the database was not saved), then sweep the whole image for E8/E9 rel32 and absolute dword
references to every function in the set. [V]

**The dead set.** The ancestors of `rcdtRsdLoad`, `rcdtTm2_LoadFile`, `rcdtTm2Load`, `rcdtTm2New`,
`rcdtRsdNew`, `rcdtChunkLoad`, `rcdtChunkNew` and `rcdtParseNew` are 54 functions. Its roots, the
members nothing references, are:

| root | what | rel32 / absolute refs |
| --- | --- | --- |
| 0x93AA40 | calls `j_FFX_SaveData_GetSceneId`, and if nonzero 0x93AA60, which builds `host:/.../guid/rsd/guid.rsd` and reaches `rcdtRsdLoad` through 0x93AD60 and `rcbgLoadText__f93A280` | 0 / 0 |
| 0x93B200 | the rcbg text loader's `3d` / `mdl` directory scan, reaches 0x93AD60 -> `rcdtRsdLoad` | 0 / 0 |
| 0x930C60 | 0x93A800 -> 0x93A890 -> `rcbgLoadText__f93BEB0` -> `rcdtTm2Load` | 0 / 0 |
| 0x923960 | `rcdtTm2LoadNoAlpha` itself | 0 / 0 |
| 0x92CCF0, 0x92CDD0 | the two rcbgDebug loaders, reach `rcdtChunkLoad` | 0 / 0 |
| 0x938A00 | `rcbgSave__f938A10` -> 0x937940 -> `FFX_RcDev_ReadMapNameCsv` -> `rcdtChunkLoad` | 0 / 0 |

The only reference into the set from outside it is `push offset SG_DebugGui_RootWindowProc` at
0x84F126, inside the orphan opener 0x84F100, which itself has 0 rel32 and 0 absolute references
(DEBUG_MODE.md section 4, and re-swept here). `SG_DebugGui_RootWindowProc` calls `rcuiPre__f930D80`,
which takes the address of `sub_935000`, which takes the addresses of the rcUI callbacks (0x9325C0, 0x933CC0, 0x934110,
0x934BC0, 0x9364D0) that reach `rcbgLoadText` and `rcbgSave`. So no dev gate is even needed: the
whole set is unreferenced from retail code. `FFX_IsDevHarness 0x887D10` does not appear on any of
these paths. [V]

**The live set.** `rcdtChunkSetMemory`, `rcdtParseInit` (with `ResetDelimiters`), `SetChunk`,
`Read`, `Skip`, `Final`, `rcdtChunkGetData`, and `rcdtChunkDelete`/`Final` reach the main loop
through `rcbgMain__f921D60` <- `FFX_RcBg_LoadYnStream 0x91AA60` <- `FFX_Map_LoadMapOutSync 0x9093B0`
and `FFX_Map_OnMapOutLoaded 0x9097C0`, the field and battle map loads. `rcbgMain__f921D60` makes its
cursor with Init, so +8 is 0 and every read is a raw copy. [V]

**The other TIM2 reader.** The yonishi image library has its own TIM and TIM2 loaders (the strings
`yiImage_loadTIM2`, `yiImageLoadTIM2`, `yiImage_readTIM2` at 0xB67514..0xB675B0). Their code at
0x918F20, 0x919370, 0x9181F0, 0x9197C0 and 0x9198D0 is not even an IDA function, and has 0 rel32 and
0 absolute references. So no retail code parses a TIM2 file at all. [V]

The PMCOM ring's `PMCOM_DATA_RSD` case in `FFX_Vfx_PumpCommandRing 0x7614A0` (0x761866) stores a
pointer the remote tool sent into a 32 byte slot table at 0xD4CA40 and does not call rcData. The ring
has no remote tool to feed it in retail (YONISHI_ENGINE.md section 8). [V]

## 6. Shipped data [M]

A scan of every entry in `FFX_Data.vbf` by extension and by the first bytes of every file:

| format | files | where | magic |
| --- | --- | --- | --- |
| `.rsd` | 230 | `yonishi_data/dat_et/encount*/rsd` (217), `dat_ov/mag_*/rsd` (13) | `@RSD940102` at 0 in all 230 |
| `.ply` | 230 | same directories, one per `.rsd` | `@PLY940102` |
| `.ma2` | 230 | same | `@MAT990928` |
| `.mat` | 0 | | |
| `.grp` | 18 | same | `@GRP940102` |
| `.vgr` | 2 | `dat_et/encount/rsd` | `@VGR940102` |
| `.tm2` | 84 | `yonishi_data` 74, `eiichi_abmap_data/maho/tim` 10 | `TIM2` at 0 in all 84 |

No other file in the archive starts with any of those magics. `FFX2_Data.vbf` has one `.tm2`
(`ffx2/master/jppc/menu/restim0.tm2`) and none of the RSD set.

Parsing every file with the loaders' own rules (an in memory emulation of the cursor, keyword table,
NextLine and token rules above):

- `.rsd`: 230 of 230 use only the six keywords, every `PLY` and `MAT` reference resolves to a
  shipped file in the same directory, every `MAT` names a `.ma2`. Every `.rsd` has a `GRP` and a
  `VGR` line, but only 18 `.grp` and 2 `.vgr` ship, which only matters to a loader that reads them,
  and this one does not.
- `.ply`: 230 of 230 have exactly one comment line between sections, all indices in range, the parse
  consumes all 239,210 bytes. 2,741 vertices, 2,348 polygons (656 triangles, 1,692 quads).
- `.ma2`: 230 of 230 counts equal their `.ply` polygon count, every line has exactly the 8 + 1 (+ 9
  for `H`) + 16 tokens the loader takes, indices are sequential. 2,328 `H` lines and 20 `G`.
- `.tm2`: all 84 are format 0, one picture, one mip level. 81 are 8bpp, 1 is 4bpp with 16 colours
  (`yonishi_data/dat/num_16.tm2`), 2 are 16 bit direct colour (both `encount*/tim/bg_1.tm2`, which
  `rcdtTm2_LoadFile` would reject). For the 82 it accepts, the bytes it reads cover each file
  exactly, 3,048,128 bytes in all.

So the shipped files are the format the dead parsers read, to the byte. They are the PS1 style tool
sources of the effect and encounter models, next to the other tool project files that name them (67
`.anm`, 10 `.par`, 7 `.otp`, 2 `.prj`).

No string in the exe names any of them. The only `.tm2` strings are the eight rcbg text loader
templates at 0xB68C18..0xB68E0C, the only `.rsd` strings are the two `guid.rsd` templates, and
`yonishi_data` appears only in the `et_ffx.bin` / `et_tex.bin` RNG seed paths and two Phyre
`TexList.txt` paths. The files the dead loaders would open (`proj/map/master/.../guid/rsd/`,
`.../2d/tex/`) do not ship at all.

## 7. Verdicts

| format | parser | retail path to the parser | shipped | verdict |
| --- | --- | --- | --- | --- |
| `.rsd` | `rcdtRsdLoad` 0x9243C0 | none, section 5 | 230 | dead, no path |
| `.ply` | `rcdtRsdLoad__LoadPly` 0x924090 | none, its only caller is `rcdtRsdLoad` | 230 | dead, no path |
| `.mat` | `rcdtRsdLoad__LoadMatRsd` 0x923BC0 | none | 0 | dead, no path and no file |
| `.ma2` | `rcdtRsdLoad__LoadMatRsd2` 0x923ED0 | none | 230 | dead, no path |
| `.grp`, `.vgr` | none, names dropped | | 18, 2 | dead, no parser |
| `.tm2` | `rcdtTm2_LoadFile` 0x9232A0, the yiImage loaders | none | 84 | dead, no path |
| rcdtChunk buffer | | `rcbgMain__f921D60` | | live, no format of its own |
| rcdtParse binary cursor | | `rcbgMain__f921D60`, `rcbgLoad.c` | | live, no format of its own, the bytes are rcBg's |
| rcdtParse text mode, `rcdtChunkLoad` | | none | | dead |

No reader was written: every file format here is dead. The live part is a memcpy cursor whose byte
layout is the rcBg stream's, which belongs in `RCBG.md` and its tool. The recreation needs nothing
from rcData beyond a cursor over a buffer.

## 8. Proposed PORTING_RULES H rows

| what | proof | doc |
| --- | --- | --- |
| rcData text loaders: `rcdtRsdLoad` and its PLY, MAT and MA2 statics, `rcdtTm2Load`, `rcdtChunkLoad`, the cursor's text mode, and the `.rsd` `.ply` `.mat` `.ma2` `.grp` `.vgr` files (230/230/0/230/18/2 ship as tool sources) | measured 2026-10-28 (1.7.7): every path up from the loaders ends at a function with 0 rel32 and 0 absolute references (0x93AA40, 0x93B200, 0x930C60, 0x923960, 0x92CCF0, 0x92CDD0, 0x938A00) or at `SG_DebugGui_RootWindowProc`, whose only reference is the orphan opener 0x84F100. No exe string names a shipped file of these types | `reversing/RCDATA.md` |
| TIM2 file loading, `rcdtTm2` and the yiImage TIM/TIM2 readers, and the 84 shipped `.tm2` | same roots for `rcdtTm2Load` 0x923800, the yiImage loaders 0x918F20, 0x919370, 0x9181F0, 0x9197C0, 0x9198D0 have 0 rel32 and 0 absolute references, the only `.tm2` strings are rcbg text loader templates for paths that do not ship | `reversing/RCDATA.md` |

## 9. Open items

- The real names of the 40 descriptive `rcdtParse` names and the six `rcdtTm2` orphans. The layout
  law constrains them (section 2) but there is no symbol list.
- MA2 fields 2..7 on each line (`1 44 8193d1a4 0 0 0` in the samples) are read and dropped by the
  loader. What MatEditor meant by them is not knowable from the exe.
- 0x919AD0 is read here as a link keeper. That is inferred from what it does (Init and Final on
  stack objects of five libraries, no use of the results) and its zero references.
- Twenty orphan rcdt functions are still not IDA functions. The apply script names their labels. Run
  `ida_funcs.add_func` on each if a decompile is wanted.
