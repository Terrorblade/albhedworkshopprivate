# FFX.exe exe-edit options (research only, nothing applied)

Target: `G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\FFX.exe`

Everything below was read straight out of the file on disk. Nothing in the binary
has been modified. All numbers are from the shipped Steam build as of 2026-10-01.

---

## 1. PE facts you need to judge any of this

```
File size                 0xA31500  (10,687,744 bytes)
e_lfanew                  0x160          PE signature at 0x160
Machine                   0x14C          i386
Optional header magic     0x10B          PE32
Linker version            11.0           MSVC 2012
ImageBase                 0x00400000
SectionAlignment          0x1000
FileAlignment             0x200
SizeOfImage               0x237D000      field at file offset 0x1B0
SizeOfHeaders             0x400          field at file offset 0x1B4
SizeOfInitializedData     0x70C000       field at file offset 0x190
CheckSum                  0xA32338       field at file offset 0x1B8
NumberOfSections          7              field at file offset 0x166
Subsystem                 2              WINDOWS_GUI
COFF Characteristics      0x0122         EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE | 32BIT_MACHINE
                                         RELOCS_STRIPPED is NOT set
DllCharacteristics        0x8140         DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE
Section header table      0x258 .. 0x370 (7 x 40 bytes)
First raw section data    0x400          -> 0x90 bytes of header slack = room for 3 more headers
```

**The exe is ASLR-relocated.** `DYNAMIC_BASE` is set and a full `.reloc` section
ships (0x1469E4 bytes), so FFX.exe does not reliably load at 0x00400000. That
does not affect an exe edit (everything in the PE is expressed as RVAs, which the
loader fixes up) but it absolutely affects any hook code: compute addresses as
`GetModuleHandle(NULL) + RVA`, never as a literal VA.

### Sections

| Name     | RVA        | VSize      | Raw ptr    | Raw size   | Characteristics | Notes |
|----------|------------|------------|------------|------------|-----------------|-------|
| `.text`  | 0x00001000 | 0x0070AA7B | 0x00000400 | 0x0070AC00 | 0x60000020 CODE R X | |
| `.rdata` | 0x0070C000 | 0x000FD7BD | 0x0070B000 | 0x000FD800 | 0x40000040 R        | holds the IAT and the import tables |
| `.data`  | 0x0080A000 | 0x019CC2C4 | 0x00808800 | 0x00081600 | 0xC0000040 RW       | VSize >> raw size: huge zero-fill tail |
| `.rodata`| 0x021D7000 | 0x00000A00 | 0x00889E00 | 0x00000A00 | 0x40000040 R        | |
| `_RDATA` | 0x021D8000 | 0x000007E0 | 0x0088A800 | 0x00000800 | 0x40000040 R        | |
| `.rsrc`  | 0x021D9000 | 0x0005CB30 | 0x0088B000 | 0x0005CC00 | 0x40000040 R        | |
| `.reloc` | 0x02236000 | 0x001469E4 | 0x008E7C00 | 0x00146A00 | 0x42000040 R DISCARDABLE | |

No `.bind` section, so there is no Steam CEG/DRM wrapper, and the import table is
not rewritten at runtime. Confirms that exe edits are viable in principle.

### Data directories present

```
EXPORT       rva 0x00809740 size 0x00007D   (FFX.exe exports NvOptimusEnablement + AmdPowerXpressRequestHighPerformance)
IMPORT       rva 0x0080551C size 0x0001A4
RESOURCE     rva 0x021D9000 size 0x05CB30
SECURITY     rva 0x00A2E600 size 0x002F00   (this one is a FILE OFFSET, not an RVA - Authenticode)
BASERELOC    rva 0x02236000 size 0x08F0E4
DEBUG        rva 0x0070D890 size 0x00001C
LOAD_CONFIG  rva 0x00792490 size 0x000040
IAT          rva 0x0070C000 size 0x0008C0
```

`BOUND_IMPORT` is empty, so there are no bound-import timestamps to invalidate.
`DELAY_IMPORT` is empty too.

### Import directory

- Directory RVA `0x0080551C`, **file offset `0x0080451C`**, size `0x1A4`.
- `0x1A4 / 20 = 21` slots: **20 real descriptors plus the all-zero terminator**
  (terminator at file offset `0x008046AC`).
- It sits in `.rdata`, which is mapped read-only. That is normal: the loader
  temporarily unprotects the pages it has to write. The existing IAT is also in
  read-only `.rdata` (IAT directory rva 0x0070C000) and the game works, which
  proves the loader does this here.
- `DataDirectory[1].VirtualAddress` is at **file offset `0x1E0`** and its `Size`
  at `0x1E4`, so the whole directory can be relocated by editing 8 bytes.
- Nothing follows the terminator: the very next bytes at `0x008046C0` are live
  `.rdata` (thunk arrays). **There are zero spare bytes to grow the array in
  place.**

Full descriptor list, load order as the loader walks it:

| # | DLL | OriginalFirstThunk | FirstThunk (IAT) | funcs | nameRVA |
|---|-----|--------------------|------------------|-------|---------|
| 0 | fmod_event.dll | 0x805DF4 | 0x70C734 | 17 | 0x806372 |
| 1 | fmodex.dll | 0x805E3C | 0x70C77C | 23 | 0x8067B6 |
| 2 | dbghelp.dll | 0x805DEC | 0x70C72C | 1 | 0x8067D6 |
| 3 | steam_api.dll | 0x805F58 | 0x70C898 | 9 | 0x8068A6 |
| 4 | KERNEL32.dll | 0x8056F0 | 0x70C030 | 112 | 0x807006 |
| 5 | USER32.dll | 0x805D0C | 0x70C64C | 41 | 0x8072DC |
| 6 | ADVAPI32.dll | 0x8056C0 | 0x70C000 | 7 | 0x807370 |
| 7 | SHELL32.dll | 0x805CE8 | 0x70C628 | 3 | 0x8073B8 |
| 8 | ole32.dll | 0x805F44 | 0x70C884 | 4 | 0x80740E |
| 9 | OLEAUT32.dll | 0x805CD8 | 0x70C618 | 3 (ordinals 2, 4, 6) | 0x807418 |
| 10 | MSVCR110.dll | 0x8059E4 | 0x70C324 | 188 | 0x80785A |
| 11 | MSVCP110.dll | 0x8058B4 | 0x70C1F4 | 75 | 0x808B2A |
| 12 | DINPUT8.dll | 0x8056E0 | 0x70C020 | 1 | 0x808B4E |
| 13 | SHLWAPI.dll | 0x805CF8 | 0x70C638 | 4 | 0x808BA6 |
| 14 | WINMM.dll | 0x805DC4 | 0x70C704 | 3 | 0x808BE2 |
| 15 | d3d11.dll | 0x805DE0 | 0x70C720 | 2 | 0x808C20 |
| 16 | IMM32.dll | 0x8056E8 | 0x70C028 | 1 | 0x808C40 |
| 17 | XINPUT9_1_0.dll | 0x805DD4 | 0x70C714 | 2 | 0x808C6E |
| 18 | VERSION.dll | 0x805DB4 | 0x70C6F4 | 3 | 0x808CC0 |
| 19 | iggy_w32.dll | 0x805E9C | 0x70C7DC | 41 | 0x809730 |

### Slack space inventory

Every section's raw tail padding, and all of it is already zero:

| Section | Padding | File offset | RVA | Usable? |
|---------|---------|-------------|-----|---------|
| `.text` | **0x185** | 0x0070AE7B | 0x0070BA7B | biggest contiguous slack in the file |
| `.rdata` | 0x43 | 0x008087BD | 0x008097BD | too small |
| `_RDATA` | 0x20 | 0x0088AFE0 | 0x021D87E0 | too small |
| `.rsrc` | 0xD0 | 0x008E7B30 | 0x02235B30 | too small |
| `.reloc` | 0x1C | 0x00A2E5E4 | 0x0237C9E4 | too small |

Largest interior zero runs (these are *live* zero-initialised data, not slack -
stomping them is a bug waiting to happen, listed only for completeness):
`.text` 0x185 @ 0x0070AE7B (that is the tail padding above), `.rdata` 0x24E @
0x007627F2, `.data` 0x2C2 @ 0x0084E536.

---

## 2. Option A - rename an existing import (smallest possible edit)

**11 bytes, one location, no header changes at all.**

The string `"DINPUT8.dll"` lives at RVA `0x00808B4E`, **file offset `0x00807B4E`**,
and is exactly 11 characters plus a NUL:

```
offset 0x00807B4A:  74 65 00 00 | 44 49 4E 50 55 54 38 2E 64 6C 6C 00 | 3D 00 50 61 74
                    "te" pad      "DINPUT8.dll\0"                       next string
```

Overwrite those 11 characters with any other 11-character DLL name, e.g.
`AlBhedWorkshop.dll` (11 chars, perfect fit, NUL stays where it is). FFX.exe then imports
`AlBhedWorkshop.dll!DirectInput8Create` instead, and our DLL forwards that one function
on to the real `dinput8.dll`.

`"dbghelp.dll"` at file offset `0x008057D6` is also exactly 11 characters if you
would rather give up crash dumps than DirectInput.

What changes: 11 bytes. What does not change: section count, section sizes, any
header field, the import descriptor array, the IAT, relocations, `SizeOfImage`.

Caveats:
- The Authenticode signature becomes invalid (nothing in the launch path checks
  it, Steam does not verify it).
- The optional-header `CheckSum` (0xA32338) becomes stale. Windows does not verify
  the checksum for user-mode images, so this is cosmetic, but recompute it if you
  want the file to look clean to tooling.
- You must be happy with an 11-character name. 11 is a hard ceiling here: the
  string is immediately followed by another string, so it cannot grow.

This is, functionally, the proxy approach with a custom filename. It buys exactly
one thing over the plain proxy: the loader DLL can be called `AlBhedWorkshop.dll` instead
of `dinput8.dll`, which means zero collision with any other mod or overlay that
wants the real `dinput8.dll` name. It costs an exe edit that Steam will revert.

## 3. Option B - add a 21st import descriptor

The textbook "add our DLL to the import table" edit. Concretely:

What has to be built (all of it new bytes somewhere):

| Item | Size |
|------|------|
| Relocated descriptor array: 20 copied + 1 new + terminator = 22 x 20 | 0x1B8 |
| New `OriginalFirstThunk` array: 1 entry + null terminator | 0x08 |
| New `FirstThunk` / IAT array: 1 entry + null terminator | 0x08 |
| `IMAGE_IMPORT_BY_NAME` (2-byte hint + `"AlBhedWorkshopInit"` + NUL) | 0x0E |
| DLL name string `"AlBhedWorkshop.dll\0"` | 0x0C |
| **Total** | **~0x1EA (490 bytes)** |

The 20 copied descriptors need no edits at all: their OFT / FT / name RVAs keep
pointing at the untouched `.rdata` originals.

Then flip `DataDirectory[1].VirtualAddress` at file offset `0x1E0` to the new
array's RVA and its `Size` at `0x1E4` to `0x1B8`.

**Where to put 490 bytes - this is where it gets awkward:**

- `.text` tail padding is the only real slack and it is `0x185` = 389 bytes.
  **490 > 389, so the full relocated array does not fit.** You could split it
  (array in `.text` padding, thunks + strings elsewhere) but you still need
  `0x1B8` = 440 bytes for the array alone, which already exceeds 389.
- Every other section's padding is 0xD0 or smaller.
- `.text` is also `R X` with no `W`, so the new `FirstThunk` would land in a
  read-only page. The loader does unprotect IAT pages it writes (the real IAT is
  in read-only `.rdata` and works), so this probably functions, but a writable
  home is strictly safer.

So Option B in practice means **adding a new section**:

1. There is room for the section header: the table ends at `0x370`, raw data
   starts at `0x400`, giving `0x90` bytes = 3 free header slots.
2. New section RVA = first 0x1000-aligned RVA past `.reloc` =
   `0x02236000 + round_up(0x1469E4, 0x1000)` = **`0x0237D000`**, which happens to
   be exactly the current `SizeOfImage`, so nothing overlaps.
3. Characteristics `0xC0000040` (INITIALIZED_DATA | READ | WRITE), VSize/RawSize
   `0x1000` / `0x200`, raw pointer = end of file.
4. Header fields to update: `NumberOfSections` 7 -> 8 at `0x166`, `SizeOfImage`
   `0x237D000` -> `0x237E000` at `0x1B0`, `SizeOfInitializedData` at `0x190`
   bumped by the new raw size, `DataDirectory[1]` at `0x1E0`/`0x1E4`, and
   optionally `CheckSum` at `0x1B8`.
5. **Blocker: the Authenticode certificate sits at the exact tail of the file**
   (`SECURITY` file offset `0x00A2E600`, size `0x2F00`, and `0xA2E600 + 0x2F00 =
   0xA31500` = EOF). You cannot append raw section data after it without the
   cert ending up in the middle of the image. You have to zero the `SECURITY`
   directory entry (strip the signature) and truncate at `0xA2E600`, then append.
   The signature is dead the moment you touch any byte anyway.

Also note: where the new descriptor goes in the array decides load order. Putting
it **last** means our DLL initialises after `iggy_w32.dll`; putting it **first**
means before `fmod_event.dll`. Either is fine for a loader that does its real work
on a bootstrap thread.

Nothing here is hard, but it is five coupled header edits plus a signature strip
plus a new section, all of which Steam undoes.

---

## 4. Verdict

**Use the proxy DLL. Do not edit the exe.**

- The proxy gets us into the process at exactly the same moment an added import
  descriptor would: both are static imports resolved by
  `LdrpInitializeProcess` before FFX.exe's entry point at RVA `0x005493C1`
  runs. There is no capability an import-table edit gives us that
  `dinput8.dll` next to the exe does not.
- Steam's *Verify integrity of game files* restores `FFX.exe`, and so does every
  game patch. An exe edit has to be reapplied, by hand or by a patcher we would
  then have to write and maintain. A dropped-in DLL survives both, and `verify`
  leaves it alone because Steam does not know about it.
- Option B needs a new section, a stripped Authenticode signature, and five
  coupled header fields updated consistently. Every one of those is a chance to
  ship a corrupt exe to a user. The proxy needs a file copy.
- Uninstalling the mod is `del dinput8.dll`. Uninstalling an exe edit is a Steam
  file verification, or restoring a backup the user hopefully kept.
- Antivirus and anti-cheat heuristics treat a modified game exe far worse than an
  extra DLL in a game folder.

The one real argument for an exe edit is naming: Option A's 11-byte rename lets
the loader be called `AlBhedWorkshop.dll`, which cannot collide with ReShade, an ASI
loader, or another mod wanting the real `dinput8.dll`. That is worth keeping in
the back pocket as a fallback **if** a user hits an unresolvable DLL-name conflict,
and it is cheap enough to automate safely (11 bytes at one fixed offset, with a
read-back check that the bytes currently say `DINPUT8.dll`). It is not worth doing
by default.

If Option A is ever needed, the exact operation is:

```
file:   FFX.exe
offset: 0x00807B4E
expect: 44 49 4E 50 55 54 38 2E 64 6C 6C        ("DINPUT8.dll")
write:  46 46 58 43 6F 6F 70 2E 64 6C 6C        ("AlBhedWorkshop.dll")
```

Keep a backup of the original `FFX.exe` (SHA-256 it first) and verify the expected
bytes before writing, so a patched game fails the check instead of getting
corrupted.
