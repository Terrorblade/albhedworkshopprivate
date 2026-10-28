# The magic textures: what the 6,433 magic files are, and how a magic id reaches them

PROGRESS 1.10.6 of Project Zanarkand. The question was what the "6,433 magic `.phyre` assets" are
and how a magic DLL references them. Done when the path from a magic id to its assets is written
out. It is, in section 3.

All exe addresses are IDA VAs (RVA + 0x400000). DLL addresses are the DLL's own VAs at base
0x10000000. The exe was read with idalib on a scratch copy of the IDB. Nothing was renamed or
saved. The archive was read in memory through `tools/vbf.py`, the textures through Project
Zanarkand's `tools/phyre.py`, and the 581 DLLs in place through `tools/magicdll.py`. No game data is
in either repo. Proposed names are in `tools/apply_magic_asset_names.py` (41 rows, a rehearsal on a
copy applied all 41 with no refusals).

Marks: **[V]** read in the binary, **[M]** measured over the whole archive or all 581 DLLs, **[I]**
inferred.

Related: `ASYNC_LOAD_REQUESTS.md` (the worker, hazards H3 and H4), `ASSET_PATHS.md` (the language
remaps), `MAGIC_DLL.md` (the host table), `EFFECT_OVERLAY.md` (the effect state machine and the
upload ring timing), `MAGIC_DLL_CONTENTS.md` (what the DLL .data holds).

---

## Summary, read this first

1. **They are not 6,433 `.phyre` files, and none of them is a mesh or an animation.** The 6,433 is
   the file count under `ffx_data/gamedata/ps3data/magic/`. 5,792 are `.dds.phyre` textures, every
   one a single `PTexture2D`. The other 641 are text: 638 `TexList.txt` manifests and three build
   leftovers in `magic_0668`. All effect geometry and motion stays in the DLL .data as PS2 data.
   Section 1. [M]
2. **The texture names are PS2 GS TEX0 values.** `<TBP0>_<PSM>_0_0_<1<<TW>_<1<<TH>.dds.phyre`. 5,442
   of the 5,467 TexList rows of the DLL ids match a TEX0 doubleword in that DLL's own .data. The PC
   remaster replaced the pixels, kept the PS2 texture records, and joins the two by TEX0. Section 5.
   [M]
3. **A DLL never holds a handle to a texture.** Its PS2 draw data carries the TEX0. At draw time the
   exe looks the TEX0 up in the loaded slot table (`FFX_MagicTex_ResolveByTex0 0x6604D0`, host 885)
   and gets back a path. A miss means the prim is not built. Section 4. [V]
4. **The textures load per effect, after the DLL, on the AsyncLoadManager worker.** The step thread
   requests `magic/magic_NNNN/tex/TexList.txt` once the DLL is in. The worker parses it and enqueues
   one LoadCluster per row. Nothing loads at DLL init and nothing is preloaded. Section 3. [V]
5. **Whether the effect start waits on them depends on a per id load mode.** Mode 1 (ids 622..628,
   348, 631, 635) starts the effect as soon as the DLL is in. Mode 2 (everything else) and mode 3
   (id 350) wait until every slot reads loaded. So H4 then H3, in sequence, and in mode 2 the step a
   spell begins on depends on disk speed. `MagicFile__selectEffectBudget` is that mode selector, not
   a budget. Section 6. [V]
6. **Host 812 `FFX_Magic_BuildTexturePath 0x906420` is misnamed and off the main path.** It is a
   2D sprite draw used by 10 DLLs (the minigame and overlay ids) that builds a texture path inline
   from a TEX0. The tklib templates at 0x88C860 are `FFX_LoadLocalizedFtc`, the text kit, and have
   nothing to do with magic. Section 4.3. [V]

---

## 1. Census

### 1.1 What is there [M]

Everything sits under `ffx_data/gamedata/ps3data/magic/magic_NNNN/`, 578 ids.

| kind | files | note |
| --- | --- | --- |
| texture `.dds.phyre` | 5,792 | 1,251,573,940 bytes, the largest 4,196,992 |
| `TexList.txt` | 638 | one row per texture, the manifest the loader reads |
| other text | 3 | `magic_0668/tex/00texlist.txt`, `texlist.log`, `texlist.pal`, a build leftover. The log holds `R:/FFX_Data/.../tex/GCM/...` paths with hashes |
| mesh | 0 | |
| animation | 0 | |

`phyre.py all` over the 5,792: 5,762 parse, each one `PTexture2D` plus one `PAssetReference`.
Formats DXT5 5,742, ARGB8 12, DXT1 8. The other 30 are refused as foreign platform (PS3 GCM)
clusters, and they are exactly the 30 textures of `magic_0668`.

### 1.2 Directories

| directory | ids | textures | TexLists | reached on PC |
| --- | --- | --- | --- | --- |
| `tex/d3d11/` | 578 | 5,535 | 577 | yes, the main set. `magic_0236` has textures and no TexList |
| `tex_us`, `tex_fr`, `tex_es`, `tex_de`, `tex_it` | 7 (0007, 0008, 0032, 0040, 0046, 0050, 0405) | 35, one per id and language | 35 | yes, for those languages. Each list names every row, the rows not in the language dir fall back to `tex/` |
| `tex_cn/` | 19 | 177 | 19 | no. Language 11 is unreachable on PC (`ASSET_PATHS.md` section 5) |
| `tex1/` | 7 (0511, 0563, 0565, 0589, 0644, 0691, 0692) | 45 | 7 | no. No exe string builds `tex1` |

Per id in `tex/d3d11`: minimum 1, median 9, maximum 44 (`magic_0519`). The big sets are 0519 (44),
then 0515, 0611, 0667 and 0668 (30 each).

Ids against DLLs: 581 DLLs, 578 asset ids, 576 have both. `magic_0687` and `magic_0713` have assets
and no DLL (0713 is also blacklisted). `magic_0052`, `0064`, `0309`, `0323` and `0507` are DLLs
with no textures.

### 1.3 The name is the TEX0 [M]

`<TBP0>_<PSM>_0_0_<w>_<h>[_N].dds.phyre`, with w and h the TEX0 `1 << TW` and `1 << TH`, not the
size of the PS2 upload. PSM is 19 (PSMT8) for 5,767 files, 0 (PSMCT32) for 11 and 20 (PSMT4) for 4.
A `_N` suffix (`_2` 36, `_1` 14, `_0` 8, `_3` 4) marks variants that share a TBP, picked at run time
(section 4.2). 10 names do not fit the pattern: the `_n` files of 0045 and 0664..0666, a `_jp` file
and `logo_hdremaster` in 0662.

Example, `magic_0021`: its effect block at 0x10006FE0 has 7 texture records (u16 count at
header+68, 32 byte records at header dword 2: +0 TEX0 low, +12 width, +14 height, +16 byte size,
+24 PSM). Their TBPs 13312, 13568, 13824, 13952, 14080, 14112 and 14176 at PSM 19 are the 7 TexList
rows exactly. Record 4 uploads 64x64, its TEX0 says TW 7 and TH 6, and the file is named `_128_64`.

### 1.4 TexList against files [M]

The `tex/` lists match their directory exactly, apart from files no list names: 0124 and 0125 (one
`14592_..._128_4` each), 0179 (one), 0563 (8), 0662 (2, one the logo), 0670 and 0671 (one each), and
0691 and 0692 (15 each, the `tex1` rows, which sit in `tex/d3d11`). No list names a file that is
missing.

---

## 2. Who builds which path

| path | builder | when |
| --- | --- | --- |
| `.../magic/magic_NNNN/tex/TexList.txt`, or `tex_<code2>/` for the 8 ids of `FFX_Magic_HasLocalizedTextures 0x65EFD0` outside jp, kr and ch | `FFX_Magic_EnqueueTexList 0x664D80` | once per effect, after the DLL is in |
| `<TexList dir>D3D11/<row>` then `tex/D3D11/<row>` when the dir ends `_xx` | `FFX_TexList_LoadAndEnqueueA 0x664550` (proposed) | per row, on the worker |
| `.../magic_%04d/tex/GCM/%d_%d_0_0_%d_%d.dds.phyre`, `tex_cn` for 0697 and 0698 under language 11 | `FFX_Magic_DrawSpriteTex0 0x906420` (was `FFX_Magic_BuildTexturePath`), host 812 | per sprite draw |
| dozens of hardcoded `PS3Data/{magic,map,menu,chr}/...` override paths | inside `FFX_MagicTex_ResolveByTex0 0x6604D0` | per lookup, for about 45 magic ids |

`FFX_fiosUnifyFilename` maps the `GCM` spelling to the PC tree. `magic_0048` is in the localized id
list but ships no `tex_xx` list, see section 7.

---

## 3. From a magic id to its textures, step by step

1. **Start.** `FFX_Btl_BeginMagicEffect 0x787F20` (or `maybe_FFX_Magic_BeginGlobalEffect 0x788C60`)
   calls `MagicFile__setMagicId 0x9DA7A0`, which folds 711 to 662, then `MagicFile__load 0x9DA430`.
   Step thread. [V]
2. **`MagicFile__load`.** In order [V]:
   - the blacklist check
   - the reuse path: if the id asked for is the previous one, still loaded, it restores it and sets
     `g_ffxMagicFileReused` (`dword_1940AB4`) at 0x9DA52A, and returns. The poll then starts the
     effect at once with the textures already resident.
   - `MagicFilePreload__preload`, which warms DLLs only, never textures
   - builds `magicFiles\FFX\magic_%04d.dll`
   - `MagicFile__selectLoadMode 0x9DAFE0` (was `selectEffectBudget`): mode 1 for 622..628, 348, 631,
     635, mode 3 for 350, mode 2 otherwise, through `FFX_Magic_SetPrxLoadMode 0x6435E0`, which writes
     `dword_CCB464` at 0x6435FA. It refuses while `FFX_LoadDll_IsBusy` or gfx+36 == 1, and then the
     previous mode stays.
   - clears `g_ffxMagicTexListRequested` (`dword_1940AB0`)
   - `FFX_LoadDllOrFatal 0x6793E0` -> `FFX_LoadDll_Impl 0x6F9BC0`. Any nonzero mode makes the DLL
     load a RegistTask on the worker, `FFX_LoadDll_AsyncWorker`. That is H4.
   - `sub_642050("magic_%04d")` sets the name at gfx+64252 that the TexList path is built from
   - only in mode 0, `FFX_Magic_RequestTexList(id)` synchronously at 0x9DA665
3. **The poll, once per step.** `FFX_MainStep_PollMagicPrxLoad 0x822250`, from the tail of
   `FFX_StepPacing`, on the step thread [V]:
   - `g_ffxBtlPendingMagicEffectId` is incremented each step it is non negative (0x822266), so it
     counts the steps waited
   - mode 0, or `MagicFile__wasReused`: start at once (0x8222DF)
   - otherwise wait for `j_FFX_LoadDll_IsDone`
   - then `MagicFile__requestTexListOnce 0x9DA280` calls `FFX_Magic_RequestTexList` once
   - mode 1: `cmp esi, 1` at 0x8222B4, `FFX_Btl_StartMagicPrx` at 0x8222C2 at once, then keep the
     request pending until the textures are in
   - modes 2 and 3: `j_FFX_Magic_AreTexturesLoaded` at 0x8222CD, and only when it returns 1,
     `FFX_Btl_StartMagicPrx` at 0x8222DF
   - the global effect runs the same code, without the reuse check, at 0x82231D, 0x82232B,
     0x822336 and 0x822348
4. **`FFX_Magic_EnqueueTexList 0x664D80`**, thiscall on `g_ffxGfxCtx`, step thread [V]:
   - adds the id to the requested list at gfx+40/+48, and returns 0 at once if it is already there
   - no name at gfx+64252: gfx+36 = 1, return 5
   - builds the TexList path (section 2), then gfx+36 = 0 at 0x664F06
   - `call maybe_FFX_IsAsyncPrxLoad` at 0x664F0A, then async: `push 2; call sub_664D10` at
     0x664F1C/0x664F1E, a RegistTask of `FFX_AsyncLoad__texListJobA 0x657880` with kind 2. Sync:
     `FFX_TexList_LoadAndEnqueue 0x665770` on the step thread.
5. **`FFX_TexList_LoadAndEnqueueA 0x664550`**, on the worker [V]:
   - no TexList file: gfx+36 = 1, return 5. Nothing loads, the effect starts with no HD textures.
   - per row: try `<dir>D3D11/<row>`, then the `tex/D3D11/<row>` fallback when the directory name
     ends in `_xx`. If neither exists it stops and returns 5 without setting gfx+36.
   - takes a free slot or grows the slot arrays (`sub_667E80`, `sub_667E00`). Slot tables on gfx,
     each a vector with its count 8 bytes before the data pointer: +60 names, +72 the 20 byte
     records, +84 full paths, +96 the loaded flags. A new slot starts with name "" and
     flag 0.
   - `AsyncLoadManager__enqueueWithContext(path, FFX_Tex_OnTexListRowLoaded)`, one LoadCluster child
     per row
   - gfx+36 = 1 at the end
6. **`FFX_Tex_OnTexListRowLoaded 0x666880`**, the LoadCluster callback, on the worker [V]:
   `PClusterArray__linkImports`, then +96[i] = 1, +60[i] = name, +84[i] = path, with no check that
   the cluster loaded. `FFX_Tex_RegisterSlot 0x656AE0` sscanf's `%d_%d_%d_%d_%d_%d` into the +72
   record: +0 kind, +4 TBP, +8 PSM, +14 w, +16 h.
7. **Ready.** `FFX_Magic_AreTexturesLoaded 0x6522B0` returns 1 only when gfx+36 is set and every slot
   with a path has flag 1, and clears gfx+36. Step thread. [V]
8. **Start.** `FFX_Btl_StartMagicPrx 0x788110`: `MagicFile__start` (`InitMagicPRX`), the overlay
   table into actor+3980, state byte actor+0xDFB = 1. [V]

TexList kinds, the same slot tables for all of them: 0 resident `et_tex`/`et_ffx` (sync, boot), 1
field `fp`, 2 magic, 3 `titl` (dead), 4 event. [V]

---

## 4. How a DLL refers to a texture

### 4.1 By TEX0, resolved at draw time [V]

A ppp Draw column, an opp kind or an Oef2 handler reads a TEX0 from the effect data and calls
`FFX_Host_ResolveTexByTex0 0x645450` (host 885, table entry 0xC65ABC). 19 DLLs also call slot 885
themselves (21 sites). It calls `FFX_MagicTex_ResolveByTex0 0x6604D0(tbp, psm, f3, f4, w, h, out
path)`:

- a one entry cache at gfx+100..+112
- hardcoded overrides for about 45 magic ids and some maps, which return 1 with a path that the
  wrapper then confirms through the cluster cache find `sub_69B4B0`
- the slot scan. It skips kind 2 when `g_ffxInMagicOverlayCall` is 0 (0x661279), kind 1 when it is
  set (0x6612B1), kind 0 while `dword_CCB9D0` is set (0x661286). It matches TBP at `[rec+4]`
  (`cmp eax, [ecx+ebx*4+4]` at 0x6612CA) and PSM at `[rec+8]` (0x6612D4).
- while a previous magic is still recorded (`MagicFile__getPrevMagicId` not -1), a kind 2 path must
  contain the current id in decimal (`strstr` at 0x661315), else the slot is skipped
- duplicate TBP variants are picked by `g_ffxMagicTexVariant` (`dword_CCB9D4`)
- a hit returns 2 and copies the path, which the draw stores in its record

A miss returns 0 and the draw does not build the prim (`sub_713790`, `if (v24 == 0) return 0`). The
by name sibling `FFX_MagicTex_ResolveByName 0x661C80` (host 840) is called by exe draws and by no
shipped DLL.

`g_ffxInMagicOverlayCall` is part of the key. A magic texture only resolves while a magic DLL's
own overlay code is running, and a field texture only outside it.

### 4.2 The variant selector [V]

`g_ffxMagicTexVariant` is written by host 222 (`FFX_Host_QueueTexRange 0x7E4830`) for ids 168/587,
170, 154, 304/568, 148/569/208, 430/588, 70/570, 307/563, 511/589, 515/667, 611/668, 519, 640 and
577, and cleared by `MagicFile__stop`. Those are the ids whose effects reuse a TBP for two images
over the life of the effect, the `_0` to `_3` files of section 1.3.

### 4.3 Host 812, the sprite path [V]

`FFX_Magic_DrawSpriteTex0 0x906420` (table entry 0xC65998) clips a quad, decodes a 64 bit TEX0,
builds `/FFX_Data/GameData/PS3Data/magic/magic_%04d/tex/GCM/<tbp>_<psm>_0_0_<w>_<h>.dds.phyre` for
the current id, unifies it, interns it in `FFX_Path_InternString 0x8AB440` (a 64 x 256 pool),
records it in the 10 entry list at `0x1875710` (`FFX_Sprite_AddTexPath 0x8E5200`) and calls
`FFX_Sprite2D_Draw`. The list is released by `FFX_Sprite_ReleaseTexPaths 0x8E89D0` from
`FFX_BtlOd_WakkaReels_StopCue` (host 649). 10 DLLs call it, 140 sites: 0565, 0644 and 0691..0698.
The language 11 branch (`cmp eax, 0Bh` at 0x9065B3) is dead.

No on demand loader was found for these paths. The other `FFX_ClusterCache_Load` callers are boot
only. So a sprite texture draws only if a TexList loaded it, which covers every file of those ids
except the 15 extra rows each of 0691 and 0692 (section 1.4). Open, section 9.

### 4.4 Unload [V]

`MagicFile__stop` -> `MagicFile__retireCurrent 0x9DA2D0` moves the id to the prev pair.
`MagicFile__unload 0x9DA960` -> `FFX_Gfx_ReleaseMagicTexList 0x670F00` drops the id from the
requested list and `FFX_TexList_Release 0x671300` frees its kind 2 slots through `sub_69BE90`. Ids
630..637 keep the 00001..00003 1x1 textures (gfx+64260), and ids 622..637 and -1 free by path.
Unload ends with `FFX_Magic_SetPrxLoadMode(1)`.

---

## 5. The PS2 textures inside the DLL .data

The PS2 records are still read, the PS2 pixels are not. [V]

- Host 222 `FFX_Host_QueueTexRange` (410 DLLs, 556 sites) walks sub table 2 (textures) and 3 (CLUTs)
  of an Oef2 block and queues each record into the 64 entry upload ring (`FFX_Tex_QueueTexUpload
  0x7E4F20`, `FFX_Tex_QueueClutUpload 0x7E4610`, `g_ffxTexUploadQueue`, `dword_11A01A0`,
  `dword_11A02A0`, head and count `dword_11A04A0`). Outside TBP 0x3800..0x3BFF, and with
  `byte_11A0066` set, it goes through the VRAM residency tracker `sub_7E56D0` instead.
- `FFX_Tex_DrainUploadQueue 0x7E4720`, from resident overlay slot 4, takes records off the ring up
  to 0x100000 bytes of PS2 record size per effects pass and hands each to `op_oef2_texture_load
  0x7E5030`, which only walks the headers. Its GS upload calls, host 425 `sub_62F820` and host 423
  `sub_62F7A0`, are `return 0` stubs, and no DLL calls either slot.
- Host 313 `FFX_Tex_IsUploadQueued 0x7E46D0` (199 DLLs) waits on that ring. The wait is counted in
  effects passes, so it is step deterministic, and it has nothing to do with the HD load.

So the PS2 texture data does three jobs on PC. Its TEX0 values are the HD file names and the
lookup key. Its byte sizes set the upload ring's timing, which effects wait on through host 313.
And its pixels are dead weight. Nothing is loaded both ways. [V]

Other texture related slots, by DLL count [M]: 820 (113), 778 (123), 871 (8), 873 (7), 907 (5),
874 (4), 867 (2), 829 (1), 815 (10). 729, 736, 739 and 740 are referenced by every DLL's thunk table.
No host slot reaches the TexList loaders, so a DLL cannot load a texture itself.

The cross check [M]: 5,442 of the 5,467 TexList rows of the 575 DLL ids with a list have a matching
TEX0 (TBP, PSM, TW, TH) in the DLL's .data. 555 DLLs match every row, 20 some, none match nothing.
The 25 misses are remaster only: synthetic TBPs (99999, 30000, 00001..00003 at 1x1, and a few like
13825, 13601, 15281, 04269) and the `_n` files. The exe uses 99999, 30000 and 10010 as literals in
`sub_713790`.

---

## 6. Load timing and threads

| what | thread | when |
| --- | --- | --- |
| `MagicFile__load`, the load mode, the DLL RegistTask | step | the step the cast begins |
| DLL `LoadLibrary`, `MagicFile__onDllLoaded` | worker | disk time later (H4) |
| `FFX_Magic_EnqueueTexList` | step | the first poll after the DLL is in |
| TexList parse, slot allocation, LoadCluster per row, the row callbacks | worker | disk time later (H3) |
| `FFX_Magic_AreTexturesLoaded`, the start | step | the first poll after the last row lands |
| the resolver lookups | the effect draws, `FFX_MagicResident_OvlEffectsPass` | every pass [I] for the thread |

[V] except the last row.

**H4 and H3 are in sequence, not parallel.** The TexList is only requested after the DLL load
finishes, so a mode 2 cast waits for the DLL, then for every texture cluster, typically 9 and up to
44 DXT5 files of up to 4 MB. [V] for the order, [M] for the sizes.

**Mode 1 ids start early.** 622..628, 348, 631 and 635 start the effect on the first poll after the
DLL is in. Until their clusters land the resolver misses and those prims are not drawn. That part is
presentation only, but the step the pending request clears on is still disk timed. [V]

---

## 7. Data gaps found

- **`magic_0236` ships 5 textures and no TexList.** Its TexList open fails, gfx+36 = 1, nothing
  loads, and no override in the resolver covers 236. Its effect draws untextured prims as misses,
  that is, not at all. [M] for the gap, [V] for the code, [I] for what the player sees.
- **`magic_0048` in us, fr, es, de and it.** It is in the localized id list and ships no `tex_xx`
  TexList, so those languages take the missing TexList branch and load none of its textures. jp, kr
  and ch read `tex/` and are fine. This answers the open half of `ASSET_PATHS.md` gap 1. [V]
- **`magic_0668`'s 30 textures are PS3 GCM clusters.** They exist, so the row check passes and the
  callback flags them loaded without checking. The cluster load itself should fail on PC. [M] for the
  format, [I] for the failure.
- `tex1`, `tex_cn` and the `magic_0668` text leftovers are never reached. [V]

---

## 8. Hazards

- **H3 and H4, the start step.** In mode 2 and 3 the step `FFX_Btl_StartMagicPrx` runs on is the
  first poll after the worker has finished the DLL and every row. That is wall clock and disk
  dependent, and two peers in lockstep will disagree. The co-op layer needs the host to publish an
  agreed ready step (a host ordered command), or the wait has to be taken out of the simulation.
  [V]
- **A missing row hangs a mode 2 cast.** If a TexList names a row that exists in neither directory,
  the loader returns with gfx+36 still 0, `FFX_Magic_AreTexturesLoaded` never returns 1, and the
  poll waits forever. No shipped list has such a row, so it is latent. A mod that edits a TexList
  can trigger it. [V] code, [M] no shipped case.
- **A request for an id still in the requested list returns without touching gfx+36.** The reuse
  path covers the normal case. A second request without an unload would leave a mode 2 cast waiting
  on a flag nobody sets. [V] code, [I] reachability.
- **The slot arrays are grown on the worker while the step thread reads them.** `sub_667E80`
  and `sub_667E00` reallocate the +60, +72, +84 and +96 arrays inside the worker loader, and
  `FFX_Magic_AreTexturesLoaded` and the resolver scan the same arrays with no lock. It is a data
  race whenever a cast grows the tables. [V] for both sides, [I] that it has ever crashed.
- **The loaded flag is not a success flag.** `FFX_Tex_OnTexListRowLoaded` sets +96 = 1 whatever the
  cluster load did. [V]
- **The lookup depends on `g_ffxInMagicOverlayCall`.** A port that runs a magic draw outside the
  overlay call, or a field draw inside it, gets different textures. [V]
- **The upload ring timing is separate and is step counted.** Host 313 waits are deterministic and
  must be kept as they are. `EFFECT_OVERLAY.md` section 10 item 4. [V]

## 9. For the recreation

- Key a magic texture by (kind, TBP, PSM, w, h), plus the variant selector, plus the current id
  substring rule. The TexList is just the list of keys to load for an id.
- Keep the PS2 texture records and the upload ring, because their byte sizes are effect timing.
  Drop the pixel upload, as the PC build does.
- Do not reproduce the disk dependent start. Either start every effect as mode 1 does and treat the
  HD textures as presentation, or gate on an explicit ready step that both peers agree on. PROGRESS
  15.6 asks for load timing that matches a recorded run. A recorded run of the original only shows
  the wait that one disk produced, so the match there can only be in the step count after the start.
- Mode 1 ids are worth checking first. They are the ones the original already ran with textures
  arriving late.

## 10. Still open

- Whether anything loads the 15 extra files each of `magic_0691` and `magic_0692`, or the other
  files no TexList names. The 2D bind seen, `sub_6A3100`, only looks up the cluster cache.
- Which thread the resolver lookups run on. They come from the effect draws, read as the step thread
  side of the effects pass, not traced to the thread.
- Who writes `dword_CCB9D0`, the kind 0 skip.
- `MagicFile__load` is also called from 0x9DB099 and 0x9DB0AC, code that is not a defined function.

## 11. Doc fixes made

- `ASSET_PATHS.md`: the `magic texture` row in section 4 now says host 812 is a sprite draw and the
  main textures come from the TexList. Gap 1 in section 8 now says what a missing TexList does.
- `ASYNC_LOAD_REQUESTS.md`: H3 now says mode 1 ids do not wait on it, and that it runs after H4.
- `FFX_GAME_NOTES.md`: the `MagicFile__selectEffectBudget` row now says it is the load mode.
- `SUMMONS.md`: the state 16 note now says mode 2 also waits for the textures.

Not edited, for the caller: Project Zanarkand `MAP.md` (lines 192, 306, 308, 361) says "6,433
magic .phyre". It is 6,433 files and 5,792 `.phyre`, all textures. PROGRESS 15.6 calls
`selectEffectBudget` a budget class, it is the load mode.
