# Reading a shipped data file at runtime

How a plugin gets at the contents of `data/FFX_Data.vbf` while the game is running, using
FFX.exe's own file layer. Nothing here reimplements the archive format, that is
[VBF_FORMAT.md](VBF_FORMAT.md) and `tools/vbf.py`.

The kit wraps all of this in `ffx/DataFile.h`. Prefer that. The rest of this file is how it
works and why, for when the wrapper is not enough.

All addresses below are VAs as IDA shows them. RVA = VA - 0x400000, and the kit's
`addresses/DataFile.h` holds the RVAs.

## The recipe

Three calls on an 8 byte object you keep on the stack. All three are `__thiscall`.

```c
struct FiosFileHandle { DWORD osHandle; DWORD vbfStream; };   // exactly 8 bytes

FiosFileHandle h = { 0xFFFFFFFF, 0 };

// 0x607F40  int __thiscall (this, path, readOnly, 0, 0, 1)
openFile(&h, "../../../ffx_ps2/ffx/proj/event/header/eventid.bin", 1, 0, 0, 1);

// success is NOT the return value, it is either half of the pair being live
if (h.osHandle != 0xFFFFFFFF || h.vbfStream != 0)
{
    DWORD n = getSize(&h);                // 0x607DC0  DWORD __thiscall (this)
    void* buf = malloc(n);
    DWORD got = read(&h, buf, n);         // 0x608090  DWORD __thiscall (this, buf, bytes)
    close(&h);                            // 0x607D80  char __thiscall (this), always 1
}
```

- **The caller allocates and frees the buffer.** Nothing in the engine's path allocates for
  you, which is the nice part: the kit uses `HeapAlloc(GetProcessHeap(), ...)` and the
  engine's allocator never enters into it.
- **One read of `getSize()` bytes from position 0 pulls the whole file.** A VBF read clamps
  the request to the bytes left in the entry, so there is no loop to write.
- **`readOnly` must be 1.** With 0 the loose file fallback calls `CreateFileW` with
  `OPEN_ALWAYS`, which CREATES an empty file on disk for a path that is not there.
- **Always `close`.** Each archive has only 15 preallocated read streams and they are shared
  with the engine's own loader. The 16th caller gets a heap allocated stream with its own
  `fopen`, so leaking them is slow rather than fatal, but still.
- Arguments 4, 5 and 6 of `openFile` are read nowhere in the function. `FFX_File_Open`
  passes `0, 0, 1`, so the kit does too. `retn 14h` confirms five stack dwords.
- The open does not modify your string. It makes a stack copy, maps `\` to `/` and
  lowercases THAT, and separately widens the ORIGINAL to UTF-16 for the `CreateFileW`
  fallback.

### The path prefix, which is the one real trap

`FFX_VbfManager__openFileStream 0x61BF10` hashes the string **starting rootPathLen bytes
in**. Straight from the disassembly at `0x61BF60`:

```
add     esi, [edi+3Ch]        ; esi = path argument, edi = manager, 0x3C = 60 = rootPathLen
mov     ecx, esi
...                           ; inline strlen from there
call    CryptHashData
```

It does not check the prefix. It skips it. `FFX_InitFileSystem 0x6795B0` calls
`FFX_VbfManager__setRootPath(mgr, "../../../")`, so **rootPathLen is 9** on this build.

So the string you pass is 9 filler bytes then the plain archive path, and the key is
`md5()` of the archive path alone. Confirmed: the stored key for
`ffx_ps2/ffx/proj/event/header/eventid.bin` is `9c18e87364e52beebc431648d290b6a0`, which is
md5 of exactly that 41 character string, no salt and no length prefix.

Read rootPathLen from `manager+60` at runtime rather than hardcoding 9, and use the
manager's own root string at `manager+64` as the filler so the loose file fallback gets a
path that means something. That is what the kit does.

### Do NOT use FFX_File_Open

There is a tidy `__cdecl` wrapper family: `FFX_File_Open 0x679730`, `FFX_File_Read 0x679800`,
`FFX_File_Close 0x679510`, `FFX_File_GetSize 0x679570`, `FFX_File_Exists 0x6796F0`.

`FFX_File_Open` is unusable from a plugin. On failure it pops a localised `MessageBoxW` out
of string resource 30774 and then `SendMessageW(hwnd, WM_CLOSE, 0, 0)`. A typo in a path
string shuts the game down. The fios layer above just reports the failure.

### Existence without opening

`FFX_fios_fileExists 0x607E00`, `__cdecl (const char* path)` with the same 9 byte prefix
rule. Asks the five mounts for the key and falls back to `CreateFileW` with `OPEN_EXISTING`,
so it creates nothing.

## Thread safety: safe from anywhere, including the render thread

**It will not deadlock against the engine's loader.** The reasoning, in the order it
matters:

1. **The read is synchronous on the calling thread.** `FFX_VbfManager__readStream 0x61BFE0`
   calls `sub_61B8D0`, which does the `_fseeki64` and `fread` inline, and `sub_61BA80`,
   which does the zlib inflate inline. There is no worker thread and no completion to wait
   for, so there is nothing that needs the game thread to pump anything.

2. **The semaphore has 30 permits and they are released by the same thread.**
   `FFX_VbfReqPool__acquireBlock 0x61B4A0` does
   `WaitForSingleObject(semaphore, INFINITE)` and then an `InterlockedCompareExchange` scan
   for a free 96 byte request block. `FFX_VbfManager__ctor 0x61BDD0` builds that pool with
   `CreateSemaphoreA(NULL, 30, 30, NULL)`. `readStream` releases the block before it
   returns. So 30 reads have to be genuinely in flight before anyone blocks, and the thing
   you would be waiting on is another thread's `fread` finishing, not a frame.

3. **Both locks are taken and released inside one call.** The critical section at
   `manager+36` guards the request pool bookkeeping. The one at `archive+68` guards the 15
   stream slots in `FFX_VbfArchive__acquireStream 0x61D9C0`. Neither is held across a
   return, so neither can be held while waiting for the game thread.

4. **The MD5 map is read only after mount.** `FFX_VbfArchive__open 0x61DAC0` builds the
   red-black tree with one `FFX_Md5Map__insert` per entry, and nothing inserts afterwards on
   a retail boot. `FFX_Md5Map__find 0x61D140` takes no lock because it does not need one.

Three residual hazards, none of them a deadlock:

- **It blocks on disk.** A cold read is a seek plus an `fread` plus an inflate. Do not do it
  per frame from a render thread. Read once and cache, which is what `ffx/GameLists.h` style
  caching is for.
- **The shared crypto provider.** `openFileStream` and `fileExists` both
  `CryptCreateHash` / `CryptHashData` / `CryptDestroyHash` against the one `HCRYPTPROV` at
  `manager+0`, acquired with `CRYPT_VERIFYCONTEXT`. The hash handles are per call, but the
  provider handle is shared. *Inferred*: rsaenh serialises internally and this is fine in
  practice, but it is the one thing on the path I cannot prove is reentrant. If you want it
  gone, compute the MD5 yourself and call `FFX_VbfArchive__openStreamByMd5 0x61DF10` on each
  mount slot directly, which skips CryptoAPI entirely.
- **Every open logs.** `FFX_fios_openFile` calls
  `Phyre_TtyPrintf(2, "[FFX_section_data_win32] %s\n", path)` before it does anything else.
  That function has a **16 KiB stack buffer**, so do not call the read path from a thread
  with a small stack, and a loop over a thousand files writes a thousand lines through
  whatever the engine's TTY callback is. Its callback singleton also lazy-inits behind a
  non-atomic flag at `0xC902E4`, but the engine hits that from 800 sites during boot, so it
  is long since set by the time a plugin exists.

**Not safe before the game has booted.** `openFileStream` returns 0 immediately when
`g_ffxVbfManager 0xCC9C48` is null, and `FFX_InitFileSystem` is what fills it. So no reads
from `DllMain`. `ffx::DataFileSystemReady()` is the check.

## Listing every shipped file, for free

`FFX_VbfArchive__open` reads the **entire** archive header into one allocation and keeps it
for the life of the process, 9,861,832 bytes for `FFX_Data.vbf`. Better still, it rewrites
each entry's stored name offset **in place** into an absolute pointer:

```
v21[4] += v40;        ; v21 = entryBase + 8, so v21[4] is entry+0x18
                      ; v40 = the name blob base
```

So the complete list of 71,979 paths is a pointer walk with no I/O and no MD5:

```c
BYTE* mgr = *(BYTE**)0xCC9C48;                      // g_ffxVbfManager
for (int slot = 0; slot < 5; ++slot)
{
    BYTE* archive = *(BYTE**)(mgr + 16 + 4 * slot);  // the 5 mount slots
    if (!archive) continue;

    DWORD* hdr   = *(DWORD**)archive;                // archive+0, the resident header
    DWORD  count = hdr[2];
    BYTE*  rows  = (BYTE*)hdr + 16 + 16 * count;     // past the md5 key array

    for (DWORD i = 0; i < count; ++i)
    {
        BYTE* e = rows + 32 * i;
        const char* path = *(const char**)(e + 0x18);   // lowercased, forward slashes
        unsigned __int64 size = *(unsigned __int64*)(e + 8);
    }
}
```

That arithmetic is the same as `tools/vbf.py`, which is verified byte for byte against the
shipped archive, so the layout is confirmed rather than inferred. `ffx::DataFileCount()` and
`ffx::DataFileEntryAt()` wrap it.

This is the answer for any picker whose options are FILES rather than table rows. A model
list is the set of `chr/<cat>/<name>/mdl/<name>.chr` paths. A map list is the `.vpa` set.

### Object layouts, for anything the kit does not wrap

`FFX_VbfManager`, 68 bytes, allocated by `FFX_VbfManager__createSingleton 0x61B590`:

| off | what |
|---|---|
| +0 | `HCRYPTPROV`, `CryptAcquireContextA(PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)` |
| +4 | request pool semaphore, 30 permits |
| +8 | pool block count, 30 |
| +12 | pool block array, 96 bytes each |
| +16 | `VbfArchive*[5]`, the mount slots |
| +36 | `CRITICAL_SECTION`, 24 bytes |
| +60 | `rootPathLen` |
| +64 | `rootPath` copy |

`FFX_VbfArchive`:

| off | what |
|---|---|
| +0 | the whole resident header block |
| +4 | MD5 red-black tree, one node per entry |
| +52 | total chunk count |
| +56 | `u16 chunkStoredSize[]`, points into the header block |
| +60 | per chunk raw/compressed flag bytes |
| +64 | the `.vbf` path, a 260 byte buffer |
| +68 | `CRITICAL_SECTION` over the stream slots |
| +92 | the 15 stream slots, 32 bytes each |

`FFX_VbfStream`, 32 bytes, 15 per archive:

| off | what |
|---|---|
| +0 | `BYTE inUse` |
| +4 | this slot's own `fopen` handle on the `.vbf` |
| +8 | owning archive |
| +12 | the 32 byte entry row |
| +16 | `u64` read position |
| +24 | 24 byte cache block, `[0]` is a 64 KiB window |

## The test case, and what a correct read looks like

`ffx_ps2/ffx/proj/event/header/eventid.bin`, 6741 bytes. Verified against the archive with
`tools/vbf.py`:

- `*(DWORD*)file` is 3216, which is both the header size and the offset of the first string.
  3216 / 8 = 402 entries.
- 402 records of 8 bytes from offset 0, each `{ DWORD stringOffset, DWORD strlenPlusOne }`.
- NUL terminated ASCII from offset 3216.
- Entry 0 is `event00`, entry 1 is `startmap0`, entry 401 is `scene9`.

This one file is why the whole exercise matters. `ffx/GameLists.h` documents that the
engine's own 402 row event id table is **empty on a retail boot**, because
`FFX_LoadEventIdTable 0x907F50` reads through `Sg_PcRead 0x83B1D0`, which sprintf's
`"host0:%s"` onto the path and looks for a loose file that no retail install has. Reading
the file out of the archive makes the event, map and cutscene pickers possible without
touching that function at all.

`Sg_PcRead` is a dead end for everything else too, for the same reason.

## Overriding a shipped file

Unchanged from [VBF_FORMAT.md](VBF_FORMAT.md), restated because it belongs next to the read
path.

- **A loose file only wins for a path the archive does not hold.** `FFX_fios_openFile` asks
  the archive first and only falls back to `CreateFileW` on a miss.
- **A second mounted archive in a lower slot wins outright.** The slot loop runs 0..4 and
  breaks on the first hit. `FFX_InitFileSystem` uses slot 0 for `FFX_Data.vbf`, so a side
  archive would need the mount order changed or an existing slot taken. Untested against the
  running game.

## What else is in there that a data-driven UI wants

The ps2 battle kernel is the item, ability and monster database, and it is sitting right
there. Three sets matter, and no single one has everything:

- `ffx_ps2/ffx/master/jppc/battle/kernel/`, 52 files, the complete original set. Japanese
  text, so good for the numbers and the id ranges and useless for labels.
- `ffx_ps2/ffx/master/inpc/battle/kernel/`, 49 files, the International build. English, and
  it has every numeric table, but **no `monster1/2/3.bin`**.
- `ffx_ps2/ffx/master/new_uspc/battle/kernel/`, 26 files, the remaster's retranslated text.
  English, has the monster tables, drops every purely numeric table.

So English monster names come from `new_uspc` and English anything-with-stats comes from
`inpc`. All three share one format, and every file in all three parses with it, which is how
I know it is the format rather than a guess.

### The kernel table format

```
+0x00  u32  1
+0x08  u16  firstId
+0x0A  u16  lastId
+0x0C  u16  recordBytes
+0x0E  u16  (lastId - firstId + 1) * recordBytes, TRUNCATED TO 16 BITS
+0x10  u32  20, the offset records start at
+0x14       records
+           string blob, at 20 + (lastId - firstId + 1) * recordBytes
```

Compute the record block size yourself from the id range and the stride. The field at +0x0E
is a `u16` and it overflows: `item_get.bin` is 366 records of 280 bytes, which is 102,480,
and the field holds 36,944.

A record's first `u16` is the offset of its name inside the blob, and the `u16` at +4 is the
offset of its description. Strings are NUL terminated. A purely numeric table has no blob,
and then both offsets are 0 and point at nothing.

### The text encoding

Not ASCII. It is ASCII 0x20..0x7A with the digits pulled to the front and `@` dropped:

| raw | character |
|---|---|
| `0x30..0x39` | `0`..`9` |
| `0x3A..0x49` | ASCII `0x20..0x2F`, so space through `/` |
| `0x4A..0x4F` | ASCII `0x3A..0x3F`, so `:` through `?` |
| `0x50..0x89` | ASCII `0x41..0x7A`, so `A` through `z` |

Everything under 0x30 is a control code. `0x00` ends the string, `0x0A` is followed by a one
byte colour id (`0xB1` highlights, `0x41` returns to normal), and `0x13` takes an argument
and inserts a value. Decoding with that table turns the raw `':200:W_:~u:~}t:rwp'` into
`" 200 HP of one cha..."`, and `'_w~t}x'` into `"Phoeni..."`.

### The useful tables

Ids are the file's own `firstId..lastId`, which is what the game's own setters take.

| file | ids | read it from | what, first row |
|---|---|---|---|
| `item.bin` | 0..111 | `inpc` | consumables. `Potion`, `Hi-Potion`, `X-Potion`, `Mega-Potion`, `Ether` |
| `command.bin` | 0..319 | `inpc` | battle commands and spells. `Attack`, `Item`, `Switch`, `Escape` |
| `a_ability.bin` | 0..133 | `inpc` | auto-abilities. `Sensor`, `First Strike`, `Initiative`, `Counterattack` |
| `c_ability.bin` | 0..83 | `inpc` | command abilities |
| `w_name.bin` | 0..169 | `inpc` | weapon and armour names. `Caladbolg`, `Brotherhood`, `Taming Sword` |
| `weapon.bin` | 0..138 | `inpc` | weapon stats |
| `monster1.bin` | 0..100 | `new_uspc` | monsters, plus a real description per row |
| `monster2.bin` | 101..180 | `new_uspc` | `Tros` is 101 |
| `monster3.bin` | 181..365 | `new_uspc` | `Iron Giant` is 181 |
| `monmagic1.bin` | 0..299 | `inpc` | monster abilities |
| `monmagic2.bin` | 0..246 | `inpc` | more of them |
| `ply_save.bin` | 0..19 | `inpc` | the characters. row 0 is `Tidus` |
| `important.bin` | 0..63 | `inpc` | key items. `Withered Bouquet` |
| `panel.bin` | 0..126 | `inpc` | sphere grid nodes. `Lv. 3 Lock` |
| `sphere.bin` | 0..49 | `inpc` | sphere types |
| `takara.bin` | 0..497 | `inpc` | treasure |
| `item_get.bin` | 0..365 | `inpc` | 100 KB of drop tables, 280 bytes a row |
| `shop_arms.bin` | 0..427 | `inpc` | shop stock, with `arms_shop` / `item_shop` / `*_rate` alongside |
| `prepare.bin` | 0..111 | `inpc` | 224 bytes a row |
| `*_txt.bin` | small | either | UI strings per screen: `menu_txt`, `btl_txt`, `config_txt`, `save_txt`, `status_txt`, `summon_txt`, `arms_txt`, `item_txt`, `mmain_txt`, `btlend_txt`, `build_txt`, `name_txt`, `help_txt` |

`btl.bin`, `magic.bin` and `help/hdd_icon.bin` do NOT use this format, they are fixed
layouts with no header, so check `u32[0] == 1` before trusting the fields.

The master tree is `jppc` (6,457 files, the complete original ps2 data), `inpc` (52, only
the battle kernel plus two battle scenes) and `uspc` (44, only the menu tree), plus eight
`new_*pc` sets for the remaster's text: `ch`, `de`, `fr`, `it`, `jp`, `kr`, `sp`, `us`.

Two things that are NOT here and still need the event table from `eventid.bin`: event and
map names. And model paths come from the file listing above, not from a table.

## Known unknowns

- **Whether `Readable(header, headerSize)` holds at runtime.** The kit guards the whole 9.8
  MB header block in one `VirtualQuery` so that walking it afterwards is a range check
  rather than a probe per entry. A 9.8 MB allocation should be its own committed region and
  so should pass, but that is reasoned rather than observed. If it fails the mount is
  skipped and `ffx::LogDataFiles()` says which, and the fix is a per-entry `Readable`.
- **Whether reading from the render thread is actually fine in a running game.** Everything
  above says it is, and every step of the reasoning came out of the disassembly, but it has
  not been run.
- **The kernel record layout past the two string offsets.** Only the name and description
  offsets are pinned down. The stats, element flags and costs in the rest of each record are
  not decoded.
- **The control codes under 0x30 in the text encoding.** `0x00`, `0x0A` and `0x13` are
  identified, the rest are not.
