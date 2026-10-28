# The save file on disk

2026-10-27. PROGRESS 1.12.5. How the PC build of FFX.exe names, lays out, checksums, writes and
reads a save file, and what that means for a co-op transplant and for a recreation that wants the
original to load its saves. The tool is `../tools/savefile.py`, and the IDB names are proposed in
`../tools/apply_savefile_names.py` (nothing renamed, nothing saved).

Marks: **[V]** read in the binary, **[D]** checked against the 12 real save files on this machine,
**[I]** my reading of the code, not proved. Addresses are VAs at the preferred base, so
RVA = VA - 0x400000. `block +N` is an offset into the 26,816 byte save block `g_ffxSaveData 0x112CA90`,
`file +N` is an offset into the file, and `file +N = block +(N - 0x40)`.

`WORLD_STATE.md` section 1 already had the header, the CRC function and the install. This document
corrects four things it got wrong (the path, the CRC, the load check, the live CRC field) and adds
the PC writer, the autosave, the slot list and the validation. The corrections are listed at the end.

## 1. Short version

- **The file** is `ffx_NNN`, no extension, NNN 000 to 199, in
  `<Documents>\Square Enix\FINAL FANTASY X&X-2 HD Remaster\FINAL FANTASY X\`. It is 26,880 = 0x6900
  bytes: a 0x40 byte header and then the 0x68C0 byte save block, copied byte for byte. No
  compression, no encryption, no padding, no per-slot container. One file is one slot. [V] [D]
- **`ffx_000` is the autosave.** The game writes it by itself at checkpoints. Manual saves go to
  001..199. [V] [D]
- **The checksum** is a CRC-16 with polynomial 0x1021, init 0xFFFF and the result inverted, but
  with a bug: table entry 0xFF is never filled and stays 0. It covers block bytes 0x0000..0x64B8 with
  the dword at block +0x64B4 counted as zero, and the result is stored twice, as the word at file
  +0x1A and as the dword at block +0x64B4. A standard CRC-16/CCITT matches none of the 12 real
  saves. The quirked one matches all 12. [V] [D]
- **Writes are in place.** `_wfopen(path, "wb")`, one `fwrite` of 26,880 bytes, `fclose`. No temp
  file, no rename, no flush, no backup. [V]
- **Everything runs on the main thread inside `FFX_MainStep`**, synchronously, one state machine step
  per simulation step. [V]
- **The load checks the CRC and nothing else that matters.** A bad CRC zeroes the staging image,
  shows a message and cancels. A language mismatch only warns. There is no size check, no version
  check and no header-to-block consistency check. [V]
- **The Steam cloud is not in the binary.** The exe imports no remote storage API and never names
  `steam_autocloud.vdf`. Steam Auto-Cloud syncs the folder from outside the game. [V], the Steam side
  [I]
- **ATEL library 12 (Save) never touches a save file in retail.** Scripts reach the save UI through
  `sg:29` with the menu request `0x40200000` (save, 634 calls in 104 packages) or `0x40100000` (load,
  the title). [V] [D]

`tools/savefile.py roundtrip` parses all 12 files, rebuilds each from its parsed fields, compares
byte for byte, then throws the stored CRC away, recomputes it and compares again. **12 of 12 byte
equal both ways.** [D]

## 2. Path and naming

### 2.1 The folder [V]

`FFX_SaveFile_EnsureSaveFolder 0x6F0330` (was `sub_6F0330`) builds the folder once and keeps it in
`g_ffxSaveFolderPath 0xCE6EE0` (wide, 512 chars), with `g_ffxSaveFolderReady 0xCE72E0` as the once flag:

```
SHGetFolderPathW(NULL, CSIDL_PERSONAL = 5, NULL, 0, docs)       0x6F035F push 5 / 0x6F0363 call
docs + "\Square Enix\"                         off_C35780 -> 0xB4B5DC   CreateDirectoryW if absent
     + "FINAL FANTASY X&X-2 HD Remaster\"      off_C35784 -> 0xB4B5F8   CreateDirectoryW if absent
     + "FINAL FANTASY X\"                      off_C35788 -> 0xB4B63C   CreateDirectoryW if absent
```

It runs from `FFX_SaveSys_Init 0x647FE0` at boot, and again at the start of every save, load and
primer import. A file name is `wsprintfW(L"%s%s%03d", g_ffxSaveFolderPath, L"ffx_", n)` with
`off_C3577C -> "ffx_"`, so the extensionless `ffx_000`..`ffx_199`. Everything goes through the wide
`_wfopen` and `CreateFileW`, so a non-ASCII Documents path works. The folder name case is whatever
already exists on disk, Windows matches it case-insensitively (this machine has `SQUARE ENIX`).

`FFX_SaveFile_ScanSlots 0x6F0A70` probes exactly 200 names, `ffx_000` to `ffx_199`, with
`CreateFileW(GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING)`. A file outside that range, or with an
extension, is invisible to the game.

### 2.2 Slot numbering [V] [D]

| file | what it is | written by |
|---|---|---|
| `ffx_000` | the autosave | `FFX_SaveFile_WriteToDisk 0x6F0510` only, section 6.4 |
| `ffx_001`..`ffx_199` | manual saves | `FFX_SaveFile_WriteSlotFile 0x6F0860` |

- A manual save to the "new" row takes **the lowest absent number from 1 to 199**. Overwriting picks
  the listed file. There is no relation between a file number and its position in the list.
- The list is ordered by the file's **`LastWriteTime`**, newest first, by
  `FFX_SaveFile_SortSlotsByDate 0x6F0FB0`, with `ffx_000` kept as row 0. The date and time the list
  shows are that file time converted to local time, not anything inside the file.
- `ffx_000` does not count toward the limit. With 199 manual files `g_ffxSaveSlotsFull 0xCE72FC` is set
  and the save list has no new row.
- The save list never offers `ffx_000`. The load list and the primer import list do.
- An edge, [I]: if the new row were taken with all of 1..199 present, `FFX_SaveFile_WriteSlotFile`
  falls back to writing `ffx_000`. The full flag hides the new row first, so I do not think this is
  reachable.

### 2.3 GameSetting.ini [V]

It is the launcher and options file, not part of the save. It sits one level up, at
`<Documents>\Square Enix\FINAL FANTASY X&X-2 HD Remaster\GameSetting.ini`, shared with FFX-2 and the
launcher. `_WinMain` builds the path at 0x62EEC1 and loads it with `FFX_Config_LoadFile 0x401CB0`.
FFX writes it with `FFX_Config_SaveFile 0x401D80` when the Esc menu opens and closes, 24 `key=value`
lines. `SETTINGS_MENU.md` section 4 has the whole format. Nothing in it is read by the save code and
nothing in a save file depends on it. The one overlap is the language: GameSetting.ini picks the
running language, and a save records the language it was written in (section 4).

### 2.4 The Steam cloud and steam_autocloud.vdf

[V] The exe imports nine functions from `steam_api.dll` (`SteamAPI_Init`, `SteamAPI_RunCallbacks`,
`SteamUser`, `SteamUserStats`, `SteamUtils`, the callback registration pair, `RestartAppIfNecessary`,
`Shutdown`) and no remote storage accessor. No string in the binary names `steam_autocloud.vdf` or a
cloud path. The `"parseCloudSaveData:data is NULL"` and `"doParseOneSaveData:data is NULL"` strings sit
in function-less code at 0x6492F0..0x6493B2 with no code or data references, console leftovers.

[I] So the cloud is Steam Auto-Cloud, configured on Steam's side, which syncs the files in the folder
before launch and after exit without the game knowing. `steam_autocloud.vdf` is the marker Steam
writes into the root of an Auto-Cloud folder. On this machine it is in the `FINAL FANTASY X&X-2 HD
Remaster` folder, next to GameSetting.ini, and it is a three line KeyValues file with one key,
`accountid`, the Steam account the folder belongs to. The game never reads or writes it.

Two consequences, both [I]:

- Steam restores files with their own timestamps, and the list order is the file time, so a cloud
  restore can reorder the list.
- The cloud root covers both games' save folders and GameSetting.ini, so all of them sync together.

## 3. The checksum

### 3.1 The function [V]

`FFX_Crc16Ccitt 0x8B1450`, `__cdecl unsigned short (int length, const BYTE *data)`:

```c
uint16_t table[256] = {0};                 // 0x8B1463..0x8B1482, a zero fill of all 256
for (unsigned i = 0; i < 0xFF; ++i) {      // 0x8B1566 cmp ecx, 0FFh / jb loc_8B1490
    uint16_t c = i << 8;                   // so i runs 0..254 and table[255] stays 0
    for (int k = 0; k < 8; ++k)
        c = (c & 0x8000) ? (c << 1) ^ 0x1021 : (c << 1);
    table[i] = c;
}
uint16_t crc = 0xFFFF;
for (int n = 0; n < length; ++n)
    crc = table[(data[n] ^ (crc >> 8)) & 0xFF] ^ (crc << 8);
return ~crc;
```

So it is CRC-16/CCITT-FALSE, inverted on output, **except that every step whose table index is 0xFF
uses 0 instead of 0xB8ED**. That happens on any input of real length, so a library CRC does not
reproduce it. The bug is in all six callers, so the game agrees with itself. [V] [D]

### 3.2 Coverage and storage [V] [D]

`FFX_SaveFile_StampCrc 0x648F20` and `FFX_SaveFile_VerifyCrc 0x647D70`, on an image `img`:

```
*(uint32_t *)(img + 0x40 + 0x64B4) = 0;               // block +0x64B4 counts as zero
crc = FFX_Crc16Ccitt(0x64B8, img + 0x40);             // block 0x0000..0x64B8
*(uint16_t *)(img + 0x1A)          = crc;             // file +0x1A
*(uint32_t *)(img + 0x40 + 0x64B4) = crc;             // block +0x64B4, zero extended
```

- The header is not covered. Nothing protects file +0x00..+0x3F.
- **Block +0x64B8..+0x68C0, the last 1,032 bytes, is not covered.** It is zero in all 12 real saves.
- `FFX_SaveFile_Serialize 0x8B3E60` computes a second CRC over the whole 0x68C0 block (0x8B3FC2) and
  stores it the same two ways. **That value never reaches disk**, because both PC writers call
  `FFX_SaveFile_StampCrc` on the image before writing (0x6F07B1 and 0x6F0534). Only the console-era
  module 15/16 verify at 0x8B3040 still uses 0x68C0, so it would reject a PC file.

### 3.3 The check [V]

`FFX_SaveFile_VerifyCrc 0x647D70` works on the save manager's staging image (`g_ffxSaveFileStaging
0x1597F70`):

1. `img[4] = 0`.
2. If `maybe_g_ffxSaveCrcSkip 0xCCB9A4` is set, `img[4] = 1` and accept. Its only writer is the orphaned
   `FFX_DebugPage_basicInfo_updateInput_ORPHAN`, so in retail this never happens.
3. Reject unless the word at file +0x1A equals **the whole dword** at block +0x64B4
   (`cmp edi, [esi+64F4h]` at 0x647D9D). A file whose block dword has a nonzero high half fails.
4. Zero the block dword (0x647DAE), compute, reject unless equal.
5. `img[4] = 1`, accept.

**Step 4 never restores the dword.** After a successful load the staging image, and so the installed
live block, holds 0 at +0x64B4.

## 4. The header [V] [D]

Written by `FFX_SaveFile_Serialize 0x8B3E60` after a `memset(img, 0, 0x40)`. Every byte is accounted
for. The PC load reads only +0x04, +0x0C and +0x1A. The slot list reads +0x05..+0x07, +0x10, +0x18
and +0x20.

| file | size | field | source | read by |
|---|---|---|---|---|
| +0x00 | u32 | counter | `++g_ffxSaveCounter 0x186627C`, clamped to 0x80000000. Bumped on every serialise, which is every checkpoint and every save menu, so it counts stagings, not saves. Reset to 0 by `sub_8B4E70` and at boot | nothing on PC |
| +0x04 | u8 | kind | Serialize argument 3, `dword_186628C`, 1 in every real file | `FFX_SaveFile_VerifyCrc` overwrites it in memory with the verified flag, `g_ffxSaveFileStagingVerified 0x1597F74` |
| +0x05 | 7 x u8 | party icons | `sub_8A9B00(i)` for i below `sub_8A9B50()`, 0xFF padded. Then +0x05 = block +0x3D58, and when the scenario word +0xBEC is above 15, +0x06 and +0x07 = block +0x3D59 and +0x3D5A, with 6 turned into 7 while the scenario word is below 300. With the scenario word at 15 or below only one member is kept | the slot list, `"_%d"` icons |
| +0x0C | u8 | language | `FFX_GetLanguage()` when written. 0 jp, 1 en, 2 fr, 3 de, 4 it, 5 es, 6 kr, 7 ch | the load, section 7 |
| +0x0D | 3 | zero | never written | nothing |
| +0x10 | u32 | playtime | block +0xBC, seconds | the slot list |
| +0x14 | u32 | gil | `FFX_SaveData_GetGil`, block +0x3D48 | nothing on PC |
| +0x18 | u16 | place name id | `FFX_SaveData_GetMapNameId`, block +0x0E or +0xD4 | the slot list, the slot picture, the autosave gate |
| +0x1A | u16 | CRC | section 3 | the load |
| +0x1C | u16 | version | hardcoded 256 | nothing |
| +0x1E | u8 | Rikku outfit | `FFX_SaveData_GetRikkuAltOutfit`, block +0xD1 | nothing on PC |
| +0x1F | u8 | environment id | `FFX_GetInternationalEnvId 0x7851D0`, which returns 0 | nothing |
| +0x20 | 32 | name | character name record 0 (block +0x634C) copied up to and including its NUL, the rest zero. FFX glyph encoded, see `CHEAT_SAVEDATA.md` section 0 | the slot list |

All 12 real files agree with every row: +0x04 = 1, +0x0D..+0x0F zero, +0x1C = 256, +0x1F = 0, the
playtime and gil equal their block fields, and the name is NUL terminated and zero filled. [D]

**None of the header is checked against the block.** An edited block with a stale header loads
fine and shows stale details in the list. `savefile.py verify` reports these mirrors as warnings,
`recompute --sync-header` refreshes playtime and gil.

## 5. The block inside the file

file +0x40..+0x6900 is the 26,816 byte block, copied with one `memcpy` by Serialize and installed
with one `memcpy` by `FFX_SaveFile_InstallBlock 0x8B54A0`. `GAME_STATE.md` and `WORLD_STATE.md` map
it. Four places where **a file's block is not the live block** it came from:

| block | in the file | in the live block | why |
|---|---|---|---|
| +0x2A | 1 | usually 0 | `FFX_SaveData_MarkLoadedFromFile` runs on the copy in Serialize. All 12 files have 1 [D] |
| +0xA8 (u16) | 0xFFFF in a manual save, the live value in the autosave | the checkpoint music | `FFX_SaveUi_BeginSave 0x6487A0` sets the staging copy to 0xFFFF around the save (0x6487D3..0x6487EE) and restores it after. All 11 manual files have 0xFFFF, the autosave has a real value [V] [D] |
| +0xAA (u16) | 0 in a manual save, the live value in the autosave | `sub_641400() + 1` | the same function, set to 0 |
| +0x64B4 (u32) | the CRC | 0 after a PC load or a new game | section 3.3 |

Where +0xA8 and +0xAA come from [V]: `maybe_FFX_Scene_Init 0x88E070`, on every checkpoint scene
(section 6.4), writes word 84 (+0xA8) = `FFX_Atel_GetCurrentBgm() | (sub_8863D0(bgm) << 8)` and word
85 (+0xAA) = `sub_641400() + 1`, next to the checkpoint location at +0xB8 and +0xBA. On load,
`FFX_SaveFile_FinishOp 0x648540` with code 3 hands +0xA8 to `dword_C526D0`, which
`FFX_AtelSys_Core_392_FadeOutCurrentBgm_resi` reads, and +0xAA to `sub_643AA0`. So a manual save
resumes with no carried music and the autosave resumes the music that was playing. [I] for what the
music handling means in play.

## 6. How the game writes and reads it

### 6.1 The pieces [V]

| what | where | role |
|---|---|---|
| `g_ffxSaveMgr 0xCE81F4` | 0x488 bytes, `FFX_SaveMgr_Create 0x6F1C10` from `FFX_SaveSys_Init` | +8 the operation mode the UI polls, +12 the result, +848 the staging image pointer, +852 its size 26,880, +868 the scan done flag |
| `g_ffxSaveFileStaging 0x1597F70` | 26,880 bytes | the one image the save UI works on. `FFX_SaveFile_StageAndDescribe 0x8B50B0` serialises the live block into it |
| `g_ffxAutosaveImage 0xCE81F0` | 26,880 byte malloc | the autosave copy |
| `FFX_SaveFile_StepSlotStateMachine 0x6F0950` | 27 states in `g_ffxSaveFileState 0xCE72E8` | all slot I/O |
| `FFX_SaveUi_Step 0x81FF80` | states in `g_ffxSaveUiState` | starts an operation, waits for the mode, commits a load |

The state machine is stepped by `FFX_MainStep_StepSaveScanAndCachePlayerPos 0x648480`, which
`FFX_MainStep` calls every step (0x820C28), and `FFX_SaveUi_Step` runs from `FFX_MainStep` too
(0x820C51). The autosave runs inside the per-actor position update `FFX_Atel_PullActorPosFromChr`, on
the same step. **So every read and write happens on the main thread, inside
a simulation step, synchronously.** `THREADS_AND_CLOCKS.md` T0 is that thread. The only off-thread
writer of the block is the play time counter at +0xBC on the vsync thread, an aligned dword.

How a script gets here [V] [D]: `sg:29` (`sgMenuInit`) lands in `FFX_MenuSys_RequestOpen 0x821750`,
which for the request `0x40200000` sets `g_ffxSaveUiState = 1` and save, and for `0x40100000` load.
A census of the 397 event packages with `tools/ebp.py` finds `sg:29 0x40200000` 634 times in 104
packages, the save spheres, and `0x40100000` 4 times in test20 (the title), test25 and testbattle.

### 6.2 A manual save, in order [V]

1. `FFX_SaveUi_Step` state 1: `FFX_SaveFile_StageAndDescribe` serialises the live block into the
   staging image (header, `MarkLoadedFromFile`, the 0x68C0 CRC) and builds the description strings.
2. `FFX_SaveUi_BeginSave 0x6487A0` blanks staging +0xA8 / +0xAA, calls `FFX_SaveMgr__beginSave`, and
   puts them back.
3. `FFX_SaveFile_BeginSave 0x6F07A0`: `FFX_SaveFile_StampCrc` on the staging image (the 0x64B8 CRC
   replaces the 0x68C0 one), `malloc` and `memcpy` it into `g_ffxSaveFileOpBuffer 0xCE7308`, set
   `g_ffxSaveFileOpInFlight 0xCE7310`, `FFX_SaveFile_EnsureSaveFolder`, state 1.
4. State 1: `FFX_SaveFile_ScanSlots` (200 `CreateFileW` probes), `FFX_SaveFile_SortSlotsByDate`,
   `FFX_SaveFile_ListSlotHeaders` (one 0x40 byte `fread` per file), state 2.
5. State 2 pick a row, state 3 confirm (message 0x389 for the new row, 0x203 to overwrite).
6. State 4: `FFX_SaveFile_WriteSlotFile 0x6F0860`: `_wfopen(path, L"wb")` at 0x6F08E1, `fwrite`
   26,880, `fclose`, state 5 with a 30 frame wait.
7. State 5: the SE `0x8000006A` and back to state 1, which rescans and relists.
8. Back out to state 6: free the copy, `FFX_SaveFile_FinishOp(1)`, state 0, op flag cleared.

**The write replaces the file in place.** No temp name, no `MoveFileEx`, no `FlushFileBuffers`, and
the `fwrite` return is not checked. A crash or a full disk mid-write leaves a short or mixed file in
that slot, which the next load rejects by CRC. A failed `_wfopen` stores to address 0 (`MEMORY[0] =
1`), a deliberate crash.

### 6.3 A load, in order [V]

1. `FFX_SaveUi_BeginLoad 0x648760`: clear staging +4, mode 3, `FFX_SaveFile_BeginLoad 0x6F01D0` with
   the staging image as the buffer, state 11.
2. States 11..13: scan, list (row 0 is `ffx_000` when it exists), pick, confirm (message 0x38C).
3. State 14: `FFX_SaveFile_ReadSlotFile 0x6F0070`: `_wfopen(L"rb")`, `fread` 26,880 **into the
   staging image, return value ignored**, then `g_ffxSaveFileLanguageMismatch 0x1133350 =
   FFX_Lang_DiffersFromRunning(file +0x0C)`. State 15 after 30 frames.
4. State 15 at 0x6EFBD0: `FFX_SaveFile_VerifyCrc`. On failure `memset(staging, 0, 26880)`, message
   0x208, state 16. On success with a language mismatch, message 0x209, state 16. Else state 17.
5. State 16 waits for a confirm, then 17. State 17: `FFX_SaveFile_FinishOp(3)` (mode 4 and the music
   restore of section 5), state 10, op flag cleared.
6. `FFX_SaveUi_Step` state 2 sees mode 4. **It commits only if `FFX_SaveUi_IsStagingVerified 0x6480A0`
   (staging +4 == 1)**, setting `g_ffxSaveUiSlotChosen`. Otherwise `sub_8B3DB0` cancels.
7. States 3 and 4: `FFX_SaveFile_CommitLoad 0x8B4EC0`, the install and re-derive sequence in
   `WORLD_STATE.md` section 1.
8. The next `FFX_MainStep` (0x820DF5) sees the language flag and calls
   `FFX_SaveData_ResetCharNamesToCurrentLanguage`, and `FFX_SaveFile_CommitLoad` starts the
   international environment swap on every load and steps it once, which only sets a busy flag and
   clears option bit 16. Corrected 2026-10-28, this said "when the saved language differs"
   (`INTERNATIONAL_CONTENT.md` 4).

### 6.4 The autosave [V]

1. `maybe_FFX_Scene_Init 0x88E070` clears `g_ffxCheckpointAutosavePending 0x23CBEBC`. For every scene
   that is not 23 (the title), not a boot scene and not one of 282, 194, 348, 349, 368, 255, 209,
   212, 270, 320, 119, 247, and not the first such scene after leaving the title, it sets the
   flag, writes the checkpoint fields (+0xB8, +0xBA, +0xA8, +0xAA) and the location history, calls
   `FFX_SaveFile_StageAndDescribe` and then `FFX_SaveFile_SnapshotForAutosave 0x6484D0`, which copies
   the staging image into `g_ffxAutosaveImage`.
2. The next `FFX_Atel_PullActorPosFromChr 0x869E40` with the flag set clears it and calls
   `FFX_SaveFile_RunAutosave 0x647E10` at 0x869F59.
3. `FFX_SaveMgr__writeAutosave 0x6F1C40`: skip when the staging header place name id is 27 (`cmp edx,
   1Bh` at 0x6F1C4A), else copy that id into the autosave image +0x18 and call
   `FFX_SaveFile_WriteToDisk 0x6F0510`.
4. `FFX_SaveFile_WriteToDisk`: `GetTickCount` into `g_ffxSaveWriteTickCount` (the 2 s icon ramp of
   `THREADS_AND_CLOCKS.md` C3), `FFX_SaveFile_StampCrc`, `FFX_SaveFile_EnsureSaveFolder`,
   `_wfopen(ffx_000, L"wb")`, `fwrite`, `fclose`. No UI, no state machine, in place.

So `ffx_000` is the block as it was when the player entered the last checkpoint scene, with the live
+0xA8 / +0xAA words.

### 6.5 The Al Bhed primer import [V]

`FFX_AtelSys_Core_453_RunMemCardCheck_start` (7 packages call core:453) starts states 21..27 with
`FFX_SaveFile_BeginPrimerImport 0x6EFEB0`. State 24, `FFX_SaveFile_ImportPrimerFromSlot 0x6EFD10`,
reads a whole chosen file into the stack **with no CRC check** and ORs its block +0x3D10 dword into
the staging image's block +0x3D10 (file offset 0x3D50 at 0x6EFD93) and into `dword_1325AC4` / `dword_1325B8C`, the mask
`FFX_AlBhed_GetImportedPrimerMask` returns. This is the only place the game reads a file without
verifying it.

### 6.6 The console-era paths [V]

- `FFX_SaveFile_ReadSlot 0x646B30` / `FFX_SaveFile_WriteSlot 0x646DF0` use
  `<dataroot>/FFX_Data/GameData/PS3Data/saves/%02d.SAV` with `fopen`. Their callers are the module
  15/16 menu `sub_8B15D0` (state 37) only.
- `FFX_SaveFile_ReadSlotNoExt 0x646CA0` reads `.../PS3Data/saves/<N>`. Its only caller,
  `FFX_SaveFile_ReadSlotToStaging`, is reached from `FFX_SaveUi_Step` state 1 when
  `maybe_g_ffxSaveUiPs3ImportSlot 0xC494CC` (-1 in `.data`) has been set to 0 by holding pad bit 0x80.
  It walks 0..199, and for every file it reads it starts a PC save of the staging image
  (`FFX_SaveUi_BeginSave`). A console save converter. [I] for the purpose.
- The retail install has only the three `.vbf` archives under `data`, so neither folder exists.

## 7. What is validated on load

| check | where | on failure |
|---|---|---|
| the file opens | `_wfopen` in state 14 | nothing is read, the staging image keeps the snapshot of the current game, and the CRC check decides. [I] that it then fails |
| file +0x1A == the block +0x64B4 dword | 0x647D9D | rejected |
| CRC over block 0..0x64B8 with +0x64B4 zero == file +0x1A | 0x647DC0 | rejected |
| file +0x0C == the running language | `FFX_Lang_DiffersFromRunning` | **warning only**, message 0x209, the load still commits and the names are reset to the running language |

**Not checked:** the file size (a short file leaves the rest of the staging image from the current
game, a long one is read to 26,880 and the rest ignored), the version word, the counter, the kind
byte on disk (it is overwritten), any header mirror, block +0x64B8..+0x68C0, and any block content.

**A bad CRC:** the whole staging image is zeroed, message 0x208 is shown, the player confirms, and
the save UI cancels through `sub_8B3DB0` with no install. The file is left as it is. The slot list
still shows it with its old header, because the list reads only the first 0x40 bytes and does not
check anything.

## 8. ATEL library 12 and the save file

`ATEL_MOVIE_SAVE_LIBRARIES.md` section 2 has the table. For the file:

- **No Save row reads or writes `ffx_NNN`.** save:0 and save:1 dump and restore 0x21EC bytes to the
  dev file `host0:atelsaveram000520.dat`, behind `FFX_IsDevHarness`, which returns 0. Dead in retail.
- save:87 (`FFX_AtelSys_Save_087_resi`) copies an image's block over the live one with no CRC check
  and no re-derive. test22 only.
- The script ABI for saving and loading is `sg:29 0x40200000` and `sg:29 0x40100000` (section 6.1),
  and for the primer import `core:453`.

## 9. Hazards

### 9.1 Co-op save transplants

1. **A file image and the live block differ in four places** (section 5): +0x2A, +0xA8, +0xAA and
   +0x64B4. A joiner that receives the host's live block and a joiner that loads the host's save file
   get different blocks. Pick one source for every peer. If it is a file, zero +0x64B4 to match what
   the PC load would have installed.
2. **The autosave writes over the joiner's own `ffx_000`.** After a transplant, the joiner's next
   checkpoint writes the host's world into the joiner's autosave, synchronously, with no prompt. A
   client should suppress `FFX_SaveFile_RunAutosave` or point it elsewhere, or the player loses their
   own autosave the first time they play as a guest.
3. **Saving is on the step, and it stalls.** One scan is 200 `CreateFileW` probes and up to 199
   header reads in a single step. A write or a read is one more. The save UI states are not
   intercepted by the lockstep layer (`MENU_SYNC.md`), so a peer saving falls behind the others by
   however long the disk takes. The autosave does the same at every checkpoint, on every peer, with a
   different disk.
4. **The language byte changes state on load.** A save from another language resets every character
   name to the running language on the next step. The international environment swap is started on
   every load whatever the language, and does nothing to the simulation (corrected 2026-10-28,
   `INTERNATIONAL_CONTENT.md` 4).
   Two peers on different languages that load the same file end with different name bytes.
5. **The header is not trusted state.** It is display only and never checked. Never take party,
   gil or playtime for a transplant from the header.
6. **`g_ffxSaveCounter` is per process and bumped on every checkpoint**, so header +0x00 differs
   between peers. It is outside the block and nothing reads it, so it is harmless, but a byte compare
   of two peers' save files will always see it.

### 9.2 The recreation writing saves the original can load

The minimum the original needs [V]:

- exactly 26,880 bytes, named `ffx_001`..`ffx_199` (or `ffx_000`) with no extension, in the folder of
  section 2.1
- the block as the original's layout (`_32` in PROGRESS 12.4)
- the CRC of section 3 **with the table quirk**, over block 0..0x64B8 with +0x64B4 zeroed, stored as
  the word at file +0x1A and as the zero extended dword at block +0x64B4

Everything else is display or convention, but to look like a file the original wrote:

- the header exactly as section 4, from the block, with +0x04 = 1, +0x1C = 256, +0x1F = 0, +0x0C the
  language the names are encoded in
- block +0x2A = 1, and +0xA8 = 0xFFFF and +0xAA = 0 for a manual save
- block +0x64B8..+0x68C0 as the block has it. It is outside the CRC, so nothing stops a wrong value,
  which is exactly why it should be right. 1.12.6 will say what it is
- the file time is the list order, so write files oldest first if a set is exported at once

Things the recreation should do better without changing a byte: write to a temporary name and
rename over the slot, flush before the rename, check the write, and do it off the simulation thread
from a copy taken at a step boundary. The original reads a file the same way whatever wrote it.

Reading original saves (PROGRESS 14.6): apply the same CRC check, decide what to do about the
language byte, and remember that `ffx_000` is a real, loadable save.

## 10. Open

- What place name id 27 is, the one place the autosave refuses to write. A lookup in the place name
  table settles it.
- Whether module 15/16 (`sub_8B15D0`, started from `FFX_Module_SwitchGameMode` and the main menu
  result switch) can reach its `.SAV` states in retail. It does not matter for the PC file, because
  the folder it would use does not exist.
  Answered 2026-10-28 (`MENU_SYSTEM.md` third pass): practically no. The main menu case 10 start is
  dead, the row mask never has bit 10, and the only other way in is game mode 0x200000 / 0x100000
  from a 0x40200000 / 0x40100000 request, which `FFX_MenuSys_RequestOpen` hands to `FFX_SaveUi`
  whenever `FFX_SaveUi_IsIdle()` is non zero. 15 is the save screen and 16 the load screen, told
  apart by `g_ffxSaveLoadMenuIsSave 0x186628A`.
- What `sub_641400() + 1` at +0xAA names, and what `sub_8863D0(bgm)` adds to +0xA8.
- Messages 0x203, 0x208, 0x209, 0x20B, 0x389, 0x38C are system text ids pushed to the Iggy menu. The
  text was not looked up.

## 11. Names proposed

`../tools/apply_savefile_names.py`, in the NAMES / RENAMES / GLOBALS framework of
`apply_sound_names.py`, plus a NOTES table of comments for five functions that keep their names
(`FFX_Crc16Ccitt` gets the table quirk, `FFX_SaveFile_VerifyCrc` the unrestored field,
`FFX_SaveFile_ReadSlotNoExt` and `FFX_SaveUi_Step` a correction, `FFX_SaveFile_WriteToDisk` the
autosave). 30 functions and 21 globals. A dry run on a copy of the database refuses nothing and every
new name is unused. The one rename of a real name is the global `ElementCount 0xCE7304`, a `fread`
parameter name that leaked onto the global, to `g_ffxSaveFileOpSize`.

## 12. Doc fixes made with this document

- `WORLD_STATE.md` section 1, the path: the PC file is `ffx_NNN` under Documents, the `.SAV` path is
  the console-era one.
- `WORLD_STATE.md` section 1, the header: party is 7 bytes at +0x05, not 3, +0x1F is
  `FFX_GetInternationalEnvId`, the name field is 32 bytes.
- `WORLD_STATE.md` section 1, the checksum: not the standard CRC, the table quirk.
- `WORLD_STATE.md` section 1, "the PC load path never checks the CRC" was wrong. It does, at load
  state 15, and a failure is refused.
- `WORLD_STATE.md` section 2, the live +0x64B4: it is 0 after a PC load, not the loaded CRC.
- `WORLD_STATE.md` open item 4 (who sets the CRC skip) settled, open item 3 (+0xA8, +0xAA) partly.
- `GAME_STATE.md`, the region map: +0x64B4 is the CRC dword and +0x64B8 starts the uncovered tail.
- The IDB comment on `FFX_SaveFile_ReadSlotNoExt 0x646CA0` says it is the PC slot list reader. It is
  the console converter's. The NOTES row in the names script carries the correction.
