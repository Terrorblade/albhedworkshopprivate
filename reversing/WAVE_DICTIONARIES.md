# WD wave dictionaries: the 865 PS2 sound banks, and why the PC never opens them

Project Zanarkand PROGRESS 8.7, written 2026-10-28. The question was whether the 865 `.wd` files
under `ffx_ps2/ffx/proj/sound/` are live in the PC build. They are not. No code path in FFX.exe
can open one, and the only part of the format the PC still reads is a 4 byte header inside copies
of the same banks that ride along in battle files and magic DLLs.

Tags: **[V]** read in the disassembly or Hex-Rays this session, **[D]** measured over the shipped
data with `../tools/wd.py`, **[I]** inferred. Addresses are IDA VAs at base 0x400000 (RVA = VA -
0x400000). The IDB was read only, section 7 lists the names I would give.

`SOUND_FORMATS.md` covers the FMOD banks and projects that replaced these, `SOUND_CLASSES.md` the
runtime classes and the dead PS2 IOP layer under the shim, `ASSET_PATHS.md` the path table.

---

## 1. Verdict

1. **The 865 files are dead.** Nothing builds a `.wd` name: the string is not in FFX.exe,
   `fmod_event.dll` or `fmodex.dll` in ASCII or UTF-16, no `cdrom.fnd` row names one, and no magic
   DLL holds one outside the sample data of an embedded bank. The PS2 loaded them on the IOP, from
   a root path the EE sent over a SIF RPC, and on PC that RPC is a stub that returns 0. [V] [D]
2. **The packed form is dead too.** The PS2 disc build read `wave_wd.sec`, a sector index, and
   sent it to the IOP with the start of `wave_wd.dat`. Every reader of that asset kind (44) is
   behind a test of a read only `.rdata` byte that is 1, so it always skips. [V]
3. **The header lives on, 4 bytes of it.** 413 battle files and 402 magic DLLs carry byte
   identical copies of the banks. When one loads, `maybe_FFX_Se_LoadProjectFromWdChunk 0x81CF90`
   checks the `"WD"` magic, reads the u16 bank number at +2, and loads FMOD project
   `SFX/<lang>/%04d.fev` with that number. Nothing else in the bank is read. [V] [D]

So the recreation ports no WD reader and no SPU ADPCM decoder. The battle and magic loaders read
`*(u16*)(chunk + 0)` for `0x4457` and `*(s16*)(chunk + 2)` for the project, and that is all.

---

## 2. The files

### 2.1 Where they are

| tree | files | notes |
| --- | --- | --- |
| `ffx_ps2/ffx/proj/sound/wave/` | 843 | 842 `wave%04d.wd` plus `smikado.wd` |
| `ffx_ps2/ffx/proj/sound/us/wave/` | 22 | US versions of 22 banks that are also in the root tree, 2 of them identical |

842 distinct bank numbers, 0 to 5000. [D]

### 2.2 Format

Square's PS2 SPU bank format, the same family Kingdom Hearts uses. [D] for every field `census`
checks, [I] for the meaning of the region fields past +0x0C.

| offset | type | field |
| --- | --- | --- |
| +0x00 | char[2] | `"WD"` |
| +0x02 | u16 | bank number, equal to the file number in all 864 numbered files |
| +0x04 | u32 | sample data size, rounded up to 64 bytes. The file is not padded, so the field runs 0, 16, 32 or 48 bytes past the end in 281, 203, 194 and 185 banks |
| +0x08 | u32 | instrument count |
| +0x0C | u32 | region count |
| +0x10 | u8[16] | zero (not in `wave0127`) |
| +0x20 | u32[n] | per instrument, the offset of its first region, 0xFFFFFFFF when empty. 818 banks pad the table to 16 bytes, 23 do not |
| then | 0x20 each | regions: +0x04 sample offset into the data, +0x08 loop offset, +0x0C SPU ADSR1 and ADSR2 (`00FF 5FC6` style values), then tuning, key range, volume and pan [I] |
| then | | the sample data, PS2 SPU ADPCM |

The sample data is SPU ADPCM without exception: all 7,421,189 16 byte frames across the 864
numbered files have a predictor of 0 to 4, a shift of 0 to 12 and loop flags of 0 to 7. [D]

Anomalies [D]:

- `smikado.wd` says bank 20 (`wave0020.wd` exists separately) and claims 787,712 bytes of sample
  data in a 558,080 byte file. A truncated or older dev copy.
- `wave2531.wd` and `wave4034.wd` have a region past the end of the data and no end flag, so they
  are truncated too.
- 11 banks list their regions out of sample order, which is shared or reordered samples, not damage.

### 2.3 The sector index ships, the packed bank does not

`ffx_ps2/ffx/proj/sound/wave/wave_wd.sec` is 5,120 u32 slots, one per bank number, holding the
start sector (2,048 bytes) of that bank inside `wave_wd.dat`, or 0xFFFFFFFF. 834 slots are used and
the starts ascend with the bank number. For 827 of the 833 gaps the next start minus this start is
exactly the shipped file in sectors, taking the `us/wave` copy where there is one, which makes the
index the US disc build. The other 6 are banks 53 to 55 and 122, which are in the index but do not
ship, and 22 and 148, which ship in a different version. 12 shipped banks are not in the index.
`wave_wd.dat` itself does not ship. [D]

### 2.4 Against the FMOD projects

| | count |
| --- | --- |
| SFX projects (`sound_pc/sfx/us/%04d.fev`, the JP set has the same numbers) | 604 |
| bank numbers that are also a project number | 600 |
| banks with no project | 242: 1, 10 to 51, 70, 71, 126 to 181 and 116 scattered ids |
| projects with no bank | 4: 617, 618, 619 and 9999, the battle streaming project |

[D]. The project number space is the bank number space, which is what lets the PC reuse the bank
number in the header (section 4). The content is not the same: in 541 of the 600 pairs the FSB5
subsound count differs from the WD region count, because a PC bank holds one rendered sound per
SE event where a WD holds instrument regions a sequence plays. The low numbered banks with no
project (10 to 51, 126 to 181) look like instrument banks for the sequenced PS2 music, which the PC
replaced with streamed recordings [I].

---

## 3. Every way a `.wd` could be opened, and why none does

### 3.1 No name anywhere

- **The executables.** A raw byte search of FFX.exe, `fmod_event.dll` and `fmodex.dll` for `.wd`,
  `wave%`, `wave_wd` and the UTF-16 forms of the first two finds nothing. The only `WD` in FFX.exe
  that means this format is the immediate `mov ecx, 4457h` at 0x81CF96. A per segment sweep of
  printable runs found `/ffx/proj/sound` (0xB5DCF0), `/ffx/proj/sound/us` (0xB5EA78) and
  `/usr/local/ffx/%s/ps2/sound` and no wave name or format. So no `sprintf` template and no string
  that the VBF lookup could hash names a `.wd`. [V]
- **The path table.** No `cdrom.fnd` row ends in `.wd`. The sound kinds are 38 (256 `music%03d.bgm`),
  44 (`wave_wd.dat` and `wave_wd.sec` under `proj/sound/us/wave/`), 45 (`music.dat`, `music.sec`)
  and two rows of kind 24 (`se000.seb`, `shout.dat`). Kind 44's two paths do not exist in the
  archive, the `.sec` ships one directory up. So the asset loader cannot reach a `.wd` whatever
  kind a caller computes. [D]
- **The magic DLLs.** 21 of the 581 DLLs contain the bytes `.wd`, and every hit is inside the ADPCM
  body of an embedded bank. [D]
- **The data.** A scan of all 70,966 other archive entries for a name ending in `.wd` (outside
  embedded bank bodies) and for `wave%`, `wave_wd` and `sound/wave` finds no `.wd` name. The only
  hits are 3 byte runs inside a `.webm` and a `.phyre`, and the PS2 build tool's path tables
  (`cdrom.fnd`, `cdrom_cd.fnd`, `cdrom_lc.fnd`, `cdrom.sc`, `cdrom.tmp` under
  `proj/battle/jp/cddata/`), which name `wave_wd.dat` and `wave_wd.sec` and nothing per bank. Of
  those only `cdrom.fnd` is loaded. [D]

### 3.2 The PS2 load path is cut at the SIF RPC

On the PS2 the IOP sound driver (`iopsound.irx`, kind 0 row 4, not shipped) owned the banks. The EE
only told it where they were, through one RPC entry point [V for the EE side, I for the IOP side]:

```
sub_887A90 0x887A90 (sound root)  -> sub_81EAB0 0x81EAB0: strcpy into off_C49458 (0x12F4180),
                                     sub_81CBD0(2)
sub_81D3F0 0x81D3F0 (wave index)  -> 20,480 byte DMA of the buffer, then sub_81CBD0(4)
sub_81D340 0x81D340 (music index) -> 2,048 byte DMA, then sub_81CBD0(5)
sub_81CBD0 0x81CBD0               -> while SG_Dev_RemotePoll_STUB_ret0 0xA44760 ...
                                     SG_Dev_RemoteRead_STUB_ret0 0xA44750(rpc, fn, 0,
                                       off_C49458, 64, off_C49460, 1024, 0, 0)
```

Both stubs are `xor eax, eax / retn` [V]. The bind used by the driver init `sub_81EFD0 0x81EFD0` is
`sub_A44740`, also `xor eax, eax / retn`, and `sub_81EFD0`'s only caller is 0x8879D9 inside an
unreferenced block at 0x8879D0 (no IDA xref, no rel32 call, no absolute dword) [V]. The DMA helper
`sub_A44770` returns -1 and `sub_A44790` returns 1 [V]. So a root path sent on PC goes into a
buffer nothing reads, and the IOP that would have opened `<root>/wave/wave%04d.wd` does not exist.

The callers of the root setter, for completeness [V]:

| caller | path | gate |
| --- | --- | --- |
| `tklib__f88D710` 0x88D884 (boot, from `FFX_MainInit`) | `/ffx/proj/sound/us` | `FFX_MesWin_GetTextLanguage() - 1 <= 5`. That byte stays 0 on PC (`ASSET_PATHS.md` 2.2), so the call never happens |
| `FFX_Btl_BeginBattle` 0x781050 | `/usr/local/ffx/%s/ps2/sound` | `FFX_IsDebugMode_dup` |
| 0x8879F5, the unreferenced block | `/ffx/proj/sound` | none, never runs |
| `FFX_AtelSys_Save_024_DevSoundFromVar_resi` 0x8788C6 | a script variable | `FFX_IsDevHarness` |

### 3.3 Kind 44 is never selected

Every function that selects asset kind 44 (or 45) starts with the same test [V]:

```
cmp  ds:byte_B5EC4F, 0
jnz  skip
```

| site | what it would do |
| --- | --- |
| 0x88D8AD in `tklib__f88D710` (boot) | read kind 44 row 1 (`wave_wd.sec`), call `sub_81D3F0` with it and row 0's sector |
| 0x88DA48 in `tklib__f88D710` (boot) | the same for kind 45 and `sub_81D340` |
| 0x88E446 `tklib__f88E440`, 0x88E3A6 `tklib__f88E3A0` | the same two, through `sub_88D1D0` |
| 0x88D480, 0x88D4B0 | kind 44 row 0's sector to `sub_81D3F0`, both unreferenced |

`byte_B5EC4F` is at 0xB5EC4F in `.rdata` (0xB0C8C0..0xC0A000, read only), holds 1, and all 17 of
its references read it [V]. A write would fault. So `jnz` always skips, and Hex-Rays drops the
blocks from `tklib__f88D710`'s pseudocode for the same reason.

How the sites were found [V]: a decompile of every function holding one of the 190
`FFX_GetAssetLoader` calls, the callers of the 14 wrappers that take the kind as an argument, and,
because Hex-Rays drops the gated blocks, a byte scan of `.text` for `push 2Ch` and `push 2Dh` next
to a slot 4 call. That scan finds exactly the sites in the table, plus the `push 2Ch` argument to
`sub_88D1D0` inside `tklib__f88E440`. The computed kinds I resolved (the menu demo table at
0xC5A870 holds 42, 19, 9 and 7, `g_ffxIntlEnvMenuAssetKind` starts at 18, the map loaders pass 19
and 26) are other values. The script supplied kinds of Save 056, Save 086 and core:470 are not
resolved here, which is why section 3.1's path table check matters: even a kind 44 read can only
reach `wave_wd.dat` or `wave_wd.sec` at paths that do not exist, never a `.wd`.

### 3.4 The one WD reader on PC

```
0x81CF90 maybe_FFX_Se_LoadProjectFromWdChunk(chunk)
  mov   ecx, 4457h
  cmp   [eax], cx             ; "WD"
  jnz   done
  movsx eax, word ptr [eax+2] ; bank number
  jmp   FFX_FmodCmd_LoadSeProject   ; 0x67A360, FMOD command 48, not resident
```

[V]. Command 48 is `FmodSfx__loadProject 0x70CF10`, which opens `SFX/US|JP/%04d.fev`
(`SOUND_FORMATS.md` 6.1). It has two callers, `sub_887450 0x887450` and 0x88752B (code with no
function). `sub_887450` waits for the voice queue to drain, skips the chunk when its size argument
is 0x30000 or more, and passes only the pointer on [V].

---

## 4. The embedded copies, where the 4 bytes are live

`wd.py embedded` [D]:

| where | files | banks | byte identical to `wave/wave%04d.wd` | distinct | largest |
| --- | --- | --- | --- | --- | --- |
| `battle/mon/*.bin` | 320 of 361 | 320 | 320 | 130 | 182,832 |
| `battle/wep/*.bin` | 74 of 74 | 74 | 74 | 20 | 55,152 |
| `battle/scn/*.bin` | 19 of 19 | 19 | 19 | 19 | 85,072 |
| `magicFiles/FFX/magic_*.dll` | 402 of 581 | 420 | 419 | 187 | 197,360 |

The PS2 effect data `yonishi_data/dat_ov/mag_*/eff_*.bin` also carries about 50, and is dead with
the rest of kind 17 (`ASSET_PATHS.md` 3).

The battle chain [V]: `FFX_Btl_LoadMonsterData 0x783680` and `FFX_Btl_OnMonsterFileLoaded
0x783990` -> `FFX_Btl_BindMonsterArchiveSections 0x783B00` (also `FFX_Btl_LoadFootData 0x783D80`
and `sub_784000` for weapons) -> `sub_7A01B0`, which walks the file's sound section table and
queues each subsection with `sub_7A06B0` into a 16 slot ring at 0x11360C8 -> `sub_7A0540`, called
from `FFX_Btl_MainStep` at 0x790CFD, which sends subsection 0 to `sub_886F30` (the `"SeSep   "`
records, `FFX_Se_LoadSeSepProjects 0x81E600`) and the others to `sub_887450`. The largest battle
bank is under the 0x30000 gate. The gate is on the subsection size, which I take to be the bank
[I].

The magic chain: `FFX_SpuStream_DrainRequests 0x800330` and `FFX_MagicResident_OvlEffectsPass
0x800670` -> `maybe_FFX_SpuStream_ServiceRequest 0x800170` -> `sub_7FFFA0 0x7FFFA0` ->
`sub_887450(*(chunk + 12), 0)`, plus the same loop inline at 0x800040..0x8000CC over
`FFX_Oef2_GetSubTable(.., 6, ..)` [V]. The size argument is 0, so the gate never applies to magic.
How a DLL's bank gets into those requests goes through the host API and I did not trace it [I].

Two embedded banks have no FMOD project: 1211 in a monster file and 1741 in a weapon file [D]. The
original asks FMOD for `SFX/<lang>/1211.fev` and gets nothing. What `FmodSfx__loadProject` does on
a missing project is not traced here, the recreation should copy it when 8.5 gets there.

---

## 5. For the recreation

- Do not port the `.wd` files, the WD region table, the SPU ADPCM data or the sector index.
  `PORTING_RULES.md` H has the row.
- Do port the 4 byte read. The battle file loader and the magic effect path hand the chunk to the
  SE layer, which checks `"WD"` and loads project `*(s16*)(chunk + 2)` through command 48. Keep the
  0x30000 size gate on the battle path and the queue order of `sub_7A0540`, one subsection per
  call, because the project load order decides which project answers an SE id (`SOUND_FORMATS.md`
  6.1).
- Do not port the SIF RPC layer, the root path or kinds 38, 44, 45. They reach a stub.

---

## 6. The tool

```
python -I tools/wd.py census      every shipped .wd checked, the sector index, the FMOD id overlap
python -I tools/wd.py embedded    the copies inside battle files and magic DLLs
python -I tools/wd.py show 1112   one bank's header and region table
```

It reads the archive through `vbf.py` and the DLLs from the install, in memory, and prints counts
and header fields only. No sample data is printed or saved.

---

## 7. Names

Applied to the IDB on 2026-10-28 with `tools/apply_wd_names.py`, 21 rows. The names this doc uses
before this section are the placeholders it was written against. Now: `FFX_Snd_LoadWdChunk 0x887450`,
`FFX_Se_LoadProjectFromWdChunk 0x81CF90` (the `maybe_` dropped), `FFX_Snd_SetSoundRoot 0x887A90`,
`FFX_SndIop_SendSoundRoot 0x81EAB0`, `FFX_SndIop_CallRpc 0x81CBD0`, `FFX_SndIop_SendWaveIndex
0x81D3F0`, `FFX_SndIop_SendMusicIndex 0x81D340`, `FFX_SndIop_Init_ORPHAN 0x81EFD0`,
`FFX_Snd_ReloadWaveIndex 0x88E440`, `FFX_Snd_ReloadMusicIndex 0x88E3A0`, `FFX_Asset_ReadWholeSync
0x88D1D0`, `FFX_Btl_QueueSoundSections 0x7A01B0`, `FFX_Btl_QueueSoundSection 0x7A06B0`,
`FFX_Btl_ServiceSoundQueue 0x7A0540`, `FFX_Snd_LoadSeSepChunk 0x886F30`, the three SG_Dev stubs
`SG_Dev_SifBindRpc_STUB_ret0 0xA44740`, `SG_Dev_SifDma_STUB_retm1 0xA44770` and
`SG_Dev_SifSetDma_STUB_ret1 0xA44790`, and the queue `g_ffxBtlSoundQueue 0x11360C8`. Two stay
guesses and carry `maybe_`: `maybe_FFX_SpuStream_LoadEffectSound 0x7FFFA0` and
`maybe_g_ffxHostFileModeConst 0xB5EC4F` (DEBUG_MODE.md had called the same byte DebugSetterGate).
The unreferenced block at 0x8879D0 has no function, so it is not named.

---

## 8. Open

- What `byte_B5EC4F` meant to the developers. It is 1 in retail and also gates the battle step
  (0x790C15, compared with 1), core:465 and `sub_88E880`. "Files come from the host, not a disc
  image" fits every use [I].
- What `FmodSfx__loadProject` does when the project is missing (banks 1211 and 1741).
- How a magic DLL's bank pointer reaches `maybe_g_ffxSpuStreamRequest`.
