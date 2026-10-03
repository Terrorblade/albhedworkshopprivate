# The VBF archive format

Everything the game loads lives in one of three archives next to `FFX.exe`:

| file | size | entries |
|---|---|---|
| `data/FFX_Data.vbf` | 19.28 GiB | 71,979 |
| `data/FFX2_Data.vbf` | 16.64 GiB | 137,386 |
| `data/metamenu.vbf` | 20.7 MiB | 150 |

All three parse with the same reader and all three verify their header MD5.

The format was read out of `FFX_VbfArchive__open` at VA `0x61DAC0`, then checked byte for byte
against the shipped `FFX_Data.vbf`. A working reader is at `../tools/vbf.py`.

Confidence: the layout below is **confirmed**, not inferred. The header MD5 recomputes to the
stored value, all 71,979 name offsets resolve to clean ASCII paths, every path's MD5 equals its
stored key, and 412 sampled files (a random 400, plus all 7 oddballs, plus the 5 largest)
decompress to exactly the size the header claims. The one field I could not explain is called out
as unknown below.

## Layout

All little endian. Offsets inside the header are absolute file offsets.

```
0x00  u32  magic = 0x4B595253, which is "SRYK" in file byte order
0x04  u32  headerSize
0x08  u32  entryCount
0x0C  u32  0
0x10       md5Key[entryCount]        16 bytes each, sorted
+          entry[entryCount]         32 bytes each, see below
+     u32  nameBlobSize
+          nameBlob                  NUL terminated lowercased paths
+          u16 chunkStoredSize[]     one per 64 KiB chunk, runs to headerSize
headerSize data, chunks stored back to back per entry
EOF-16     md5 of bytes [0, headerSize)
```

`headerSize` doubles as the absolute offset of the first entry's data, so there is no padding
between the header and the payload.

### Entry, 32 bytes

| off | type | meaning |
|---|---|---|
| `+0x00` | u32 | first chunk index, into `chunkStoredSize[]` |
| `+0x04` | u32 | **packer scratch, ignore it.** See below |
| `+0x08` | u64 | uncompressed size |
| `+0x10` | u64 | absolute file offset of this entry's first chunk |
| `+0x18` | u32 | name offset, relative to the start of `nameBlob`. The loader rewrites this in place to an absolute pointer, which is why the disassembly shows a `+=` on it |
| `+0x1C` | u32 | always 0 |

On `+0x04`: `FFX_VbfArchive__open` never reads it, and comparing all three archives shows it carries
nothing. In `FFX_Data.vbf` and `FFX2_Data.vbf` it is the same constant `0x003813F6` on every entry
that has chunks (71,972 and 130,631 of them) and `0` on every zero-length entry (7 and 6,755). The
same constant in two unrelated archives rules out anything derived from the content. And in
`metamenu.vbf` it is not that constant at all, it exactly duplicates `firstChunkIndex` on all 150
entries. So it is a leftover field in the packer that happened to hold different garbage in
different tool versions, and it goes to 0 when an entry has no chunks to process. A reader should
not look at it and a writer can put anything there.

### Lookup

The key is `md5(path)` where the path is lowercased, uses forward slashes, and has no leading
slash and no archive root prefix. Nothing more, no salt and no length prefix. Confirmed on
sampled paths at both ends of the table.

`FFX_VbfManager__openFileStream 0x61BF10` hashes the path starting `manager+60` bytes in, which is
how the mount's root prefix gets skipped, then calls `FFX_VbfArchive__openStreamByMd5 0x61DF10` on
each mount slot.

The sorted MD5 array on disk is **only used to build an in-memory red-black tree** at archive+4, one
`FFX_Md5Map__insert 0x61DE30` per entry during `open`. Lookups are `FFX_Md5Map__find 0x61D140`
against that tree, not a binary search of the on-disk array. (Both of those functions were named
`findEntryByMd5` / `findFile` in an earlier pass. That was wrong twice over: they search the tree,
and they return a read stream rather than a pointer to the 32 byte entry row.)

**Mount precedence is settled.** The slot loop runs 0..4 and breaks on the first hit, so the
**lowest numbered slot wins** on a duplicate path. A second mounted archive is therefore a real
override mechanism, provided it lands in a lower slot than `FFX_Data.vbf`.

Reads go through a pool of **15 preallocated 32 byte streams** at archive+92, handed out under the
critical section at archive+68 by `FFX_VbfArchive__acquireStream 0x61D9C0`. If all 15 are busy it
allocates a 16th with its own `fopen` handle, so 15 is a fast path, not a concurrency limit.

Because the key is only the MD5, the archive does not actually need the path strings to work. They
are there for tooling and for the fallback-to-loose-file path.

### Chunking and compression

Every entry is cut into 64 KiB (`0x10000`) logical chunks. `chunkStoredSize[]` is global across the
whole archive, and an entry's chunks are `chunkStoredSize[firstChunk .. firstChunk + ceil(size/0x10000))`.

Decoding one chunk, given `stored = chunkStoredSize[i]`:

- `stored == 0` means the chunk was kept raw at the full 64 KiB. Read 64 KiB.
- For an entry's **final** chunk only, `stored == (size & 0xFFFF)` also means raw. Read `stored` bytes.
- Otherwise it is a zlib stream of `stored` bytes, expanding to 64 KiB, or to the remainder for the
  final chunk.

That second rule is the compressor giving up on an incompressible tail. It is not a guess: the
loader builds a byte array of per-chunk "is compressed" flags, initialises it as
`flag[i] = (chunkStoredSize[i] != 0)`, and then per entry clears the flag on the last chunk when
the stored size equals the exact remainder. `../tools/vbf.py` implements the same two tests.

The sum of all uncompressed sizes is 28.71 GiB against a 19.28 GiB file, so the whole archive is
only about a third compressed by volume. Much of the bulk is already-compressed `.webm` video and
`.fsb` audio that deflate cannot touch.

## Writing an archive

Not implemented yet, but there are no unknowns blocking it. The header MD5 is genuinely checked, so
a rebuilt archive has to recompute the trailing 16 bytes. Two easier routes exist for modding and
should be preferred:

1. **Loose file override.** `FFX_fios_openFile 0x607F40` looks the path up in the VBF and falls
   back to `CreateFileW` on a real file **only if the path is not in the archive**. So a loose file
   wins only for paths the archive does not already contain. Overriding an existing asset this way
   needs the archive lookup to miss, which means either patching that function or removing the entry.
2. **Mount a second archive.** The manager has **5 mount slots** at manager+16 and only one is used.
   The slot search runs 0..4 and takes the first hit, so an archive in a lower slot shadows
   `FFX_Data.vbf` entirely. This looks like the cleanest override route: no repacking of a 19 GB
   file, no MD5 to recompute on it, and a small side archive can hold just the changed assets.
   Untested against the running game.

## What is actually in FFX_Data.vbf

Two parallel trees, which is the thing to understand before going looking for anything.

`ffx_data/gamedata/ps3data/...` is the HD remaster content: 47,125 `.phyre` files (PhyreEngine
serialized assets), D3D11 textures, remastered geometry.

`ffx_ps2/ffx/master/<lang>pc/...` is the **original PS2 data, shipped intact**, and that is where
the game logic data lives. Languages are `jp`, `us`, `fr`, `de`, `es`, `it`, `kr`, `ch`, plus
`new_*pc` variants.

Top directories by file count:

| path | files |
|---|---|
| `ffx_data/gamedata/ps3data/map` | 26,926 |
| `ffx_data/gamedata/ps3data/chr` | 7,364 |
| `ffx_ps2/ffx/master/jppc` | 6,457 |
| `ffx_data/gamedata/ps3data/magic` | 6,433 |
| `ffx_data/gamedata/ps3data/sound_pc` | 3,831 |
| `ffx_data/gamedata/ps3data/event` | 3,180 |
| `ffx_data/gamedata/ps3data/btlmap` | 2,734 |

Inside `ffx_ps2/ffx/master/jppc`, which is the interesting half:

| subtree | files |
|---|---|
| `chr` | 3,500 (`mon` 1,623, `npc` 1,081, `obj` 212, `skl` 197, `pc` 140, `sum` 136, `wep` 111) |
| `battle` | 1,371 (`btl` 863, `mon` 361, `wep` 74, `kernel` 52, `scn` 19) |
| `event` | 931 |
| `map` | 433 |
| `btlmap` | 59 |
| `help`, `help_inter` | 53 each |
| `menu` | 48 |

Extensions worth knowing, archive-wide:

| ext | count | what it is |
|---|---|---|
| `phyre` | 47,125 | PhyreEngine serialized asset |
| `bin` | 8,452 | generic PS2 data, includes `menu_script.bin`, `battle_script.bin`, `system_script.bin` |
| `mgrp` | 3,156 | motion group. `FFX_Mot_RegisterBundle 0x836C90` asserts with the tag `SG:Add MGRP` |
| `txt` | 2,365 | mostly `texlist.txt` manifests |
| `ftc`, `ahwin32`, `ah` | 1,535 / 1,324 / 2 | `ahwin32` looks like a platform-baked `ah` |
| `fev`, `fsb` | 1,260 each | FMOD event project and sound bank |
| `chr` | 865 | PS2 character container, `chr/<cat>/<name>/mdl/<name>.chr`. See `CHR_FORMAT.md` |
| `wd` | 865 | **sound wave dictionaries** under `ffx_ps2/ffx/proj/sound/`, nothing to do with characters. The 865 == 865 match with `.chr` is a coincidence and I initially read it as a pairing |
| `vpa` | 491 | **map container**, ASCII magic `MAP1` despite the extension, and the walkmesh lives in it. See `MAP_FORMAT.md` |
| `ebp` | 397 | **event script**, magic `EV01` |
| `ply`, `rsd`, `ma2` | 230 each | PS2 geometry and material, paired |
| `omd` | 237 | |
| `an2`, `anm` | 158 / 67 | animation |
| `webm` | 98 | video, libwebm/mkvparser |
| `sps2`, `tm2`, `clt` | 130 / 84 / 62 | PS2 texture and palette formats |

The 7 zero-length entries are leftovers from the original build machines and are a useful canary
for a reader, since they are the only entries where the `+0x04` field is 0:

```
ffx_data/gamedata/ps3data/map/azit/azit00/tex/d3d11/temp
ffx_data/gamedata/ps3data/lockit/placeholder
ffx_ps2/ffx/yonishi_data/dat_ov/mag_0154/par/mfc71c0.tmp
ffx_ps2/ffx/yonishi_data/dat_ov/mag_0154/par/mfcd144.tmp
ffx_ps2/ffx/master/jppc/btlmap/kino/kino01_a/bin/mapout.$$$
ffx_ps2/ffx/master/jppc/battle/kernel/magic.$$$
ffx_ps2/ffx/master/inpc/battle/kernel/magic.$$$
```

## A first look at three of the formats inside

Only skimmed, not decoded. Noted here so the next pass does not start from zero.

### `menu/*_script.bin` - the PS2 menu layout scripts

`menu_script.bin` (18,736 bytes), `battle_script.bin` (35,584) and `system_script.bin` (1,008), one
set per language. All three open with a **0xD0 byte table of 52 u32 entry point offsets**, zero for
an unused slot, every non-zero value in range and pointing at a record. 33 of 52 slots are used in
the menu and battle scripts, 14 in the system one.

The records contain a repeating 4 byte shape `u16 offset, u8 00, u8 kind` where the offset points
forward inside the file, so it reads as a node graph rather than flat bytecode. Interleaved with
those are NUL terminated names, and the names say what each file is for:

- `menu_script.bin` and `system_script.bin` are **UI layout**. 489 and 22 names, prefixed by what
  they describe: `pos_` position, `col_` colour, `rect_` rectangle, `num_` count, `kpos_` and
  `fkpos_` kana position, plus `r_` and `c_` variants. Examples: `rect_cbline`, `col_helpplt0`,
  `pos_cname`, `num_cnameplt`, `pos_filestr`, `col_mssgwind1`.
- `battle_script.bin` is something else: 211 names like `kuro`, `kuro30`, `siro`, `libsiro`,
  `limmura1`, `norkuro2`, `dum59`, `eventy10`. Those read as battle scene or camera cut names
  (kuro and siro are Japanese for black and white), not widgets.

This matters for the mod because a second player needs HUD elements, and this is where the existing
HUD geometry is defined.

### `event/**/*.ebp` - the event scripts

397 of them, magic `EV01`, a 0x40 byte header holding 6 section offsets with the tail zeroed, and
the last offset equal to the file size. These are the ATEL event scripts. Under active analysis, see
`FFX_GAME_NOTES.md`.

### `chr/pc/*.chr` and `.wd`

865 of each, paired one to one, 140 of them under `chr/pc` which is the playable characters. Under
active analysis.

## Using the reader

```
cd tools
python vbf.py info
python vbf.py find event/obj/az
python vbf.py extract ffx_ps2/ffx/master/jppc/event/obj/az/azit0000/azit0000.ebp -o azit0000.ebp
python vbf.py extract-all master/jppc/event/ -o out
python vbf.py list --out names.txt
```

From Python, which is the better route for bulk work:

```python
from vbf import VbfArchive
with VbfArchive(r"...\data\FFX_Data.vbf") as ar:
    data = ar.read("ffx_ps2/ffx/master/jppc/event/obj/az/azit0000/azit0000.ebp")
```

Opening the archive parses the whole 9.4 MB header, which takes a couple of seconds, so hold one
`VbfArchive` open rather than reopening per file.

## Known unknowns

- **How to get a second archive mounted.** Precedence is settled (lowest slot wins), but what calls
  the mount function and whether anything other than `FFX_InitFileSystem 0x6795B0` can add a slot is
  not. Reading the callers of the archive-open path would settle it.
- **Whether the game tolerates a rebuilt archive.** The header MD5 is checked, but nothing else
  obviously is. Untested, and testing it needs the game running.
