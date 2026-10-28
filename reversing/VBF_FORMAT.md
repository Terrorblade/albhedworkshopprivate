# The VBF archive format

Three archives ship next to `FFX.exe`. FFX.exe mounts only `FFX_Data.vbf` and FFX-2.exe only
`FFX2_Data.vbf`. `metamenu.vbf` is the launcher's (`FFX&X-2_LAUNCHER.exe`), and no game exe opens
it, see `METAMENU.md`. This line used to say everything the game loads lives in one of the three.

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
+     u32  nameBlobSize              counts ITSELF plus the blob, so the chunk table is at
+                                    (offset of this field) + nameBlobSize
+          nameBlob                  NUL terminated lowercased paths
+          u16 chunkStoredSize[]     one per 64 KiB chunk, runs to headerSize
headerSize data, chunks stored back to back per entry
EOF-16     md5 of bytes [0, headerSize)
```

`headerSize` doubles as the absolute offset of the first entry's data, so there is no padding
between the header and the payload.

On `nameBlobSize`: the value includes its own four bytes. In `metamenu.vbf` the field holds 7618
and the names occupy 7614 bytes, and the chunk table starts exactly at the field's offset plus 7618
(checked 2026-10-11, the byte before it is the final NUL and the remaining header length is even).
`tools/vbf.py` has always computed it this way, the text above used to leave it implicit.

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
override mechanism only if it lands in a lower slot than `FFX_Data.vbf`, and the engine's own mount
function cannot put it there. See "The mount slots" below.

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

## The mount slots

Settled 2026-10-28 (Project Zanarkand item 1.4.4). Everything here is static reading of `FFX.exe`,
nothing was run in the process. Names below are the ones `../tools/apply_mount_names.py` proposes,
with the old `sub_` name alongside where it changes.

**The answer in one line.** Nothing but `FFX_InitFileSystem` ever fills a mount slot, it fills
exactly slot 0 with `data\FFX_Data.vbf`, slots 1 to 4 are written only with zero, and the only
other writer clears all five at process exit. There is no loose folder mount, no `host0:` mount, no
debug or dev mount, no save path mount and no mod hook in the binary.

### The manager

`g_ffxVbfManager 0xCC9C48` holds a pointer to one 68 byte heap object, allocated by
`FFX_VbfManager__createSingleton 0x61B590` (`FFX_Alloc(0x44)`) and freed by
`FFX_VbfManager__destroySingleton 0x61B690` (was `sub_61B690`). It has no vtable.

| off | what | written by |
|---|---|---|
| +0 | `HCRYPTPROV` | ctor (0, then `CryptAcquireContextA`) |
| +4 | request pool semaphore | createSingleton (0), `FFX_VbfReqPool__init 0x61C2C0`. Closed but not cleared by `FFX_VbfReqPool__dtor 0x61AF40` and destroySingleton |
| +8 | pool block count, 30 | createSingleton (0), ReqPool init |
| +12 | pool block array, 30 x 96 bytes, heap | createSingleton (0), ReqPool init, destroySingleton (0), ReqPool dtor (0) |
| **+16, +20, +24, +28, +32** | **`VbfArchive* slot[5]`** | **the table below** |
| +36 | `CRITICAL_SECTION`, 24 bytes, so it ends at +59 | `InitializeCriticalSection`, `Enter`/`Leave`, `DeleteCriticalSection` |
| +60 | `rootPathLen` | ctor (0), `FFX_VbfManager__setRootPath 0x61C3A0` |
| +64 | `char*` root path, a heap copy | ctor (0), setRootPath, `FFX_VbfManager__unmountAll 0x61B620` (0) |

The "+16..+36" in the progress item is the five slot dwords, +16 through +35. +36 is the first byte
of the critical section, and the API calls that write it never reach +35.

A slot points at a 96 byte `VbfArchive` from `FFX_Alloc(0x60)`, built by `FFX_VbfArchive__ctor
0x61D460` (was `sub_61D460`) and destroyed by `FFX_VbfArchive__dtor 0x61D670` (was `sub_61D670`).

### Every writer of the five slots

| VA | function | stores | when | retail |
|---|---|---|---|---|
| `0x61BDF6`, `0x61BDFD`, `0x61BE04`, `0x61BE0B`, `0x61BE12` | `FFX_VbfManager__ctor 0x61BDD0` | 0 into +16, +20, +24, +28, +32 | boot, once, from createSingleton inside `FFX_InitFileSystem` | yes |
| `0x61C1D1` `mov [edi+10h], eax` | `FFX_VbfManager__mountArchive 0x61C150` | the new `VbfArchive*` (or 0 if `FFX_Alloc` failed) into the first slot that reads 0. `edi` is `this + 4*i` from `lea edi, [edi+esi*4]` at `0x61C1C7`, and `i` is 0..4 because the scan at `0x61C180` returns at `i == 5` | boot, once. The only caller is `0x6795F7` in `FFX_InitFileSystem` with `data\FFX_Data.vbf`, and the ctor has just zeroed all five, so `i` is 0 | yes, slot 0 only |
| `0x61C1FF` `mov dword ptr [edi+10h], 0` | `FFX_VbfManager__mountArchive` | 0 into the same slot after `FFX_VbfArchive__open 0x61DAC0` fails, once the half built archive is destroyed and freed | boot, only on a failed open (missing file, bad `SRYK` magic, header MD5 mismatch) | yes |
| `0x61B666` `mov dword ptr [esi], 0` | `FFX_VbfManager__unmountAll 0x61B620` (was `sub_61B620`) | 0 into each non-null slot after `FFX_VbfArchive__dtor` and `FFX_Free`. `esi` starts at `lea esi, [ebx+10h]` (`0x61B643`, `ebx` = `this`), steps 4, `ebx` counts 5 down | exit, once: `FFXApplication__exitApplication 0x42F600` (vtable entry at `0xB0D9B0`) -> `FFX_ShutdownFileSystem 0x679590` (was `sub_679590`) -> `FFX_VbfManager__destroySingleton` -> here | yes, at exit |

Three more paths reach those same stores and can never run, because nothing in the image refers to
them: `FFX_VbfManager__construct_ORPHAN 0x61AD70` (calls the ctor), `FFX_VbfManager__dtor_ORPHAN
0x61AFA0` and `FFX_VbfManager__deletingDtor_ORPHAN 0x61B1D0` (both call `unmountAll`). They are the
compiler's out of line copies of the special members that `createSingleton` and
`destroySingleton` inline.

So in a retail run the slots go through exactly three states: all zero (ctor), slot 0 =
`FFX_Data.vbf` and 1..4 zero (mount), all zero again (exit). Slots 1 to 4 never hold a pointer.

**A failed mount does not stop the boot.** `FFX_InitFileSystem` shows a `MessageBoxW` with string
resource 30774, then `FFX_App_SendCloseToMainWindow 0x42F4D0` (was `sub_42F4D0`) sends `WM_CLOSE`
to `g_phyreMainHwnd`, and initialisation carries on. Until the window closes every slot is null,
`openFileStream` misses on every path, and every read falls through to a loose file.

### How the census was done, and why nothing is missed

The slots are inside a heap block, so any writer has to hold a pointer to that block. The proof is
that the pointer has exactly one home and every copy of it can be followed.

1. **Every reference to the pointer's home.** A raw byte scan of all 35,487,744 loaded bytes (every
   segment, code and data, analysed or not) for any little endian dword in
   `[0xCC9C00, 0xCC9CA0)`. It finds the 7 instructions IDA already lists for `g_ffxVbfManager` and
   two unrelated ones (`0x61C621` and `0x9D9C53`, below). Any absolute access to the global has to
   encode its address or a base within 0x48 below it, so there is no eighth. *Inferred, not
   scanned:* an indexed access from a base further away would need an array that spans the
   global. In the IDB it is its own 4 byte item, and the nearest array below it, `dword_CC9B30`
   (48 bytes), ends at `0xCC9B60`. The 7 are: the `cmp` at `0x61B5B4` in createSingleton
   and its one store at `0x61B5FB` (the `FFX_Alloc` result), the load and the zeroing store in
   destroySingleton, the load in `FFX_VbfManager__get 0x61BDB0`, and a `cmp` each in `fileExists`
   and `openFileStream`.
2. **Every caller of every function that takes the pointer.** A second raw scan for `E8`/`E9` rel32
   and `0F 8x` rel32 branches whose target is any of 25 manager, archive and request functions
   (the orphan `0x61B730` included), and for any dword equal to one of their addresses (a vtable, a host API table, a `push offset`). The
   branches it finds are exactly IDA's code xrefs, every hit sits on an instruction head, and there
   is no dword anywhere equal to a method address. So there is no vtable, no function pointer table
   and no unanalysed caller. `get` has 5 callers: `FFX_fios_fileExists 0x607E98`,
   `FFX_fios_openFile 0x607FE8`, `FFX_VbfStream__read 0x61AC0B` and `FFX_InitFileSystem` twice
   (`0x6795DF`, `0x6795F0`).
3. **Where each copy of the pointer goes.** A flow sensitive register taint over each function that
   receives it (basic blocks from IDA's flow chart, union at joins, stack spills through
   `[ebp+x]` followed as pseudo registers, offsets widened to "unknown" in loops). It starts from
   `ecx` for each method, from `eax` after each call to `get`, from `eax` after `FFX_Alloc` in
   createSingleton and from `edi` after the global load in destroySingleton, and it reports every
   memory write through a tainted base, every tainted index, every tainted push, every thiscall
   with a tainted `ecx`, every store of the pointer itself, and any `rep stos`/`rep movs`. Result:
   - the 5 `get` callers only hand it on as `ecx` to `fileExists`, `openFileStream`, `readStream`,
     `setRootPath` and `mountArchive`. None stores it or returns it.
   - the only stores of the pointer are the global (`0x61B5FB`) and C++ unwind slots `[ebp+var_10]`
     and `[ebp+var_4]` in the ctor and dtor frames.
   - slot writes are the ones in the table and nothing else. The "unknown offset" writes are the
     two in `mountArchive` and the one in `unmountAll`, and their loop bounds are checked by hand
     above.
   - the pointer is passed to Win32 only as `&mgr+0` to `CryptAcquireContextA` (writes 4 bytes) and
     `&mgr+36` to the critical section calls (24 bytes, +36..+59).
   - `readStream` passes it to `FFX_VbfManager__buildReadRequest 0x61B7E0` (was `sub_61B7E0`),
     which keeps it in a local and passes `mgr+4` down through `FFX_VbfReqPool__beginRequest
     0x61B740` to `FFX_VbfReqPool__acquireBlock 0x61B4A0`. Those write only into a 96 byte request
     block on the heap, never into the manager. No request block, archive or stream stores the
     manager pointer.
   - no memset, memcpy or string op covers the manager.
4. **Unwind funclets.** The C++ unwind maps (FuncInfo `0xBF48D0`, `0xBF4954`, `0xBF4980`,
   `0xBF4A04`, `0xBF48FC`, `0xBF4928`, `0xBF49D8`) list every action that runs if one of these
   frames unwinds. Each one is either `FFX_Free([ebp+var_10])` or `FFX_VbfReqPool__dtor` on
   `mgr+4`, which writes +12 and closes +4. None touches a slot.

The two unrelated window hits: `0x61C621 lea ebx, unk_CC9C50[eax*4]` is a two entry ring of 60
byte records toggled by `dword_CC9CC8` (0 or 1, so `ebx` is `0xCC9C50` or `0xCC9C8C`), right after
`PRenderContextD3D11__applyDrawState`, and its stores land at `+0x10` and up from there. It never
reaches `0xCC9C48` and could not touch a slot anyway, since the slots are on the heap.
`0x9D9C53` writes `byte_CC9C17`.

What the census does not cover: a stray write through an unrelated pointer into the heap block
(no evidence of one, and nothing in the image computes the block's address any other way), code
loaded into the process from outside `FFX.exe`, and the allocator's own free list writes after
`destroySingleton` has freed the block. The magic DLLs get only the host table that
`MAGIC_DLL.md` describes, and step 2 shows no table in `FFX.exe` holds a manager function. The
co-op kit only reads the slots (`loader/workshop/src/ffx/DataFile.cpp`).

### How a lookup walks the slots

1. A caller builds the path. Most callers go through `FFX_fiosUnifyFilename 0x679820`, which prepends
   `g_ffxDataRoot 0xCC9698` (`../../..`) and applies the `.cgfx` and `GCM` rewrites, so the string
   starts with the 9 byte root.
2. `FFX_fios_openFile 0x607F40` (thiscall on an `FFX_fios_FileHandle`, `+0` HANDLE, `+4` stream).
   It logs `[FFX_section_data_win32] %s`, widens the ORIGINAL string to UTF-16 for later, and on a
   separate copy maps `\` to `/` and lowercases (`FFX_fios_toLowerInPlace 0x607D50`).
3. `FFX_VbfManager__openFileStream 0x61BF10` returns 0 at once if `g_ffxVbfManager` is null.
   Otherwise it MD5s the string from `+[mgr+60]` bytes in (`add esi, [edi+3Ch]` at `0x61BF60`),
   then walks `slot[0]` to `slot[4]` (`0x61BF98` to `0x61BFBC`), skips a null slot, calls
   `FFX_VbfArchive__openStreamByMd5 0x61DF10` with `ecx` = the slot, and stops at the first non-zero
   stream. Priority is slot order, and in retail only slot 0 is ever populated.
4. On a hit the stream goes in `handle+4` and the open returns 0. On a miss it falls back to
   `CreateFileW` on the UTF-16 copy of the original string, case and backslashes intact. Read mode
   (`a3 != 0`) is `FILE_READ_DATA`, `FILE_SHARE_READ`, `OPEN_EXISTING`,
   `FILE_FLAG_SEQUENTIAL_SCAN`. Write mode is `FILE_WRITE_DATA`, no sharing, `OPEN_ALWAYS`, and if
   that fails a read only probe that returns 12 when the file exists. Not found returns 10.
5. `FFX_fios_fileExists 0x607E00` is the same walk through `FFX_VbfManager__fileExists 0x61BE40`
   and `FFX_VbfArchive__hasEntryMd5 0x61DEC0` (`0x61BECD` to `0x61BEEA`), then the same
   `CreateFileW` read open. It calls `SetEndOfFile` on that handle before closing it. *Inferred*
   from the Win32 contract: a handle opened with `FILE_READ_DATA` only cannot set the end of file,
   so the call fails and nothing is truncated.

Reads then stay on the stream. `FFX_VbfStream__read 0x61AA90` serves its 64 KiB window and calls
`FFX_VbfManager__readStream 0x61BFE0`, which never touches the slots.

### Things called "mount" that are not VBF mounts

None of these reads or writes `g_ffxVbfManager`, which step 1 proves for all of them at once.

- `sub_779490` (`Mount pfs`, `hdd0:%s,%s`, `hdd_mount_in`) and `sub_779730` (`hdd_unmount_in`) are
  the PS2 HDD partition mount that `FFX_SaMenu_SetMovieStreamFromHdd` calls
  (`ATEL_MOVIE_SAVE_LIBRARIES.md`). It has its own once flag, `byte_112A508`.
- `FFX_Map_MountDevVpa 0x909680` and `FFX_Map_MountMapStream 0x90F8E0` hand a map section blob to
  the map code (`MAP_SECTIONS.md`).
- `Sg_PcRead 0x83B1D0` prefixes `host0:` and reads a loose file. That is a path, not a mount.
- `metamenu.vbf` is never mounted by `FFX.exe` (`METAMENU.md`).

### Publish order, for anything that mounts at runtime

*Inferred, not observed.* `mountArchive` stores the archive pointer into the slot at `0x61C1D1`
BEFORE it calls `FFX_VbfArchive__open` at `0x61C1DD`, and takes no lock. At boot nothing else is
reading yet, so it does not matter. A plugin or a recreation that mounts while the async loader or
the render thread is opening files would publish an archive whose MD5 tree `FFX_Md5Map__insert` is
still building, and on a failed open the slot is non-null for the whole open and then freed. Open
the archive first and publish the pointer last.

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
   `FFX_Data.vbf` entirely. No repacking of a 19 GB file and no MD5 to recompute on it, and a small
   side archive can hold just the changed assets. The catch, corrected 2026-10-28: calling
   `FFX_VbfManager__mountArchive` after boot puts the side archive in slot 1, BEHIND `FFX_Data.vbf`,
   because it takes the first free slot and slot 0 is already full. A side archive mounted that way
   only adds paths the main archive does not have, which a loose file already does. To shadow, it
   has to be mounted before the engine's own mount (a hook between `setRootPath` and `mountArchive`
   inside `FFX_InitFileSystem`) or the slot pointers have to be swapped by hand. Untested against
   the running game. See "The mount slots" for the publish order hazard.

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

Decoded since, see `MENU_SCRIPT.md` and `tools/menuscript.py`. In short: `menu_script.bin` (18,736
bytes), `system_script.bin` (1,008) and `battle_script.bin` (35,584) share one format. A 0xD0 byte
header holds 16 value hash buckets, 16 scene hash buckets, a sequence table offset, 3 zero words and
16 draw groups. Values are named constants (`rect_`, `pos_`, `col_`, `num_`) and scenes are
keyframed 2D animations. menu_script has 412 values and 35 scenes, system_script 16 and 2,
battle_script 191 and 34.

What this skim had wrong, corrected 2026-10-28:

- The header is not 52 entry point offsets. It is the 16 + 16 + 1 + 3 + 16 layout above.
- The repeating 4 byte shape is not `u16 offset, u8 00, u8 kind`. It is a value entry header, the
  name offset in the low 24 bits and the number of values that follow in the high 8.
- The name counts are 447, 18 and 225, not 489, 22 and 211. The scan also counted colour words that
  happen to be printable.
- There are no `kpos_` or `fkpos_` names.
- `battle_script.bin` is not scene or camera cut names. It is the battle HUD in the same format.
  `kuro` (0x80000000) and `siro` (0x80FFFFFF) are black and white colour values, and the `b` names
  (`bnormal`, `btidu`, `bwakk`) are command window and overdrive minigame scenes.
- The game does not load one set per language. It loads the jppc `menu_script.bin` and
  `system_script.bin` for every language, and only `battle_script.bin` swaps. system_script is never
  read at all.

The HUD geometry for a second player is in `battle_script.bin`, the menu geometry in `menu_script.bin`.

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

- ~~How to get a second archive mounted.~~ Settled 2026-10-28, see "The mount slots". Nothing but
  `FFX_InitFileSystem 0x6795B0` ever fills a slot, and it fills exactly slot 0.
- **Whether the game tolerates a rebuilt archive.** The header MD5 is checked, but nothing else
  obviously is. Untested, and testing it needs the game running.
