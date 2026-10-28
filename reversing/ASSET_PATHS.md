# Asset paths, remaps and languages

Written 2026-10-13 for Project Zanarkand item 1.4.7. Every address is an IDA VA in FFX.exe (image
base 0x400000, RVA = VA - 0x400000). `[V]` is read in Hex-Rays or the disassembly this session,
`[I]` is inferred. The IDB was read only, so every `sub_` below is unnamed there and section 9
proposes names.

The checker is `E:\Git\projectzanarkand\tools\asset_paths.py`. It rebuilds every path family in
this doc for all nine language columns, runs each through the same unify and archive key steps
the game uses, and looks it up in `FFX_Data.vbf` (and `metamenu.vbf` as a second opinion). It reads
the path table, the video lists and FFX.exe at run time and copies nothing.

```
python -I E:\Git\projectzanarkand\tools\asset_paths.py [--tsv OUT] [--show N]
```

## 1. From a name to an archive entry

Every read goes through the same three steps. [V]

1. **A builder** makes a path. It is one of three shapes:
   - `/FFX_Data/GameData/PS3Data/...`, the PC asset tree,
   - `/ffx_ps2/ffx/...`, the PS2 tree as shipped,
   - `host0:/ffx/...`, a PS2 dev host path. These go through `FFX_File_OpenHost0 0x62FA30`, which
     does `strcpy(buf, "/ffx_ps2/")` then appends `path + 7`. It does not check that the first 7
     bytes are `host0:/`.
2. **`FFX_fiosUnifyFilename 0x679820`**:
   - if the path contains `.cgfx.phyre`, everything from there is replaced by `.fx.phyre`,
   - the data root `../../..` (`FFX_fios_getDataRoot`) is put in front unless the path already
     contains it,
   - the **first** `GCM` is replaced by `D3D11`. Case sensitive, so `gcm` and `GNM` survive.
3. **The VBF manager** skips the first 9 characters without looking at them, which is `../../../`
   for a well formed path, then looks up the md5 of the lowercased rest without a leading slash.
   When the lookup misses, `FFX_fios_openFile` falls back to a loose file (`CHEAT_WARP.md` 12.2).

So `/FFX_Data/GameData/PS3Data/menu/GCM/x.dds.phyre` is archive entry
`ffx_data/gamedata/ps3data/menu/d3d11/x.dds.phyre`, and `host0:/ffx/master/jppc/a.bin` is
`ffx_ps2/ffx/master/jppc/a.bin`. A path that does not start with `/` (a raw `host0:` string handed
straight to unify) becomes `../../..host0:/...`, loses `../../..h` to the skip, and looks up
`ost0:/...`, which never exists. Section 6 has the two places that happens. [V]

### 1.1 Archives

`FFX_InitFileSystem` mounts only `data\FFX_Data.vbf` (71,979 entries). `metamenu.vbf` (150
entries) belongs to the launcher and is never opened by FFX.exe. The `/MetaMenu/...` timestamp
paths FFX.exe does hold resolve into a separate 15 entry `metamenu/` tree inside `FFX_Data.vbf`,
not into `metamenu.vbf`, see `METAMENU.md` section 4. There are no per language archives, every
language lives in `FFX_Data.vbf` side by side. [V]

## 2. Language numbering

There are two language values and they are not the same thing.

### 2.1 The PC language, `FFX_GetLanguage 0x6410E0`

`LanguageManager` singleton `dword_CDED58` (`sub_6DAFC0` creates, `sub_6DB070` gets, `sub_6DB040`
destroys, ctor `sub_6DAEE0`). `sub_6DB090` fills it once from `g_ffxGameSettings[0]`. `+0` is the
master version, `+4` the language, `+8` a western flag. `j_FFX_GetLanguage 0x8AC2F0` is a thunk.
[V]

| settings byte | master (+0) | language (+4) | code | +8 |
|---|---|---|---|---|
| 0 and anything not listed | 0 Japan | 0 | jp | 0 |
| 1 | 1 America | 1 | us | 1 |
| 2 | 1 | 2 | fr | 1 |
| 3 | 1 | 4 | de | 1 |
| 4 | 1 | 5 | it | 1 |
| 5 | 1 | 3 | sp | 1 |
| 6 | 3 Asia | 9 | kr | 0 |
| 7 | 3 | 10 | ch | 0 |

Master 2 (Europe) is never selected. Language 11 (cn, simplified Chinese) and 18 (uk) are cases in
many switches but no code writes them, so they are unreachable on PC. The ninth column of every
table here is 11, because the binary carries a `cn` row in most families. [V]

### 2.2 The PS2 text language, `g_ffxMesWinTextLanguage 0x1330830`

A byte read by `FFX_MesWin_GetTextLanguage_impl 0x887D00`. Its only writer is
`FFX_MesWin_SetTextLanguage_impl 0x887D20`, called from the ATEL Save library function 93
(`FFX_AtelSys_Save_093_resi 0x878470`) and the debug menu `sub_7CE450` case 45. Nothing on the PC
language path sets it, so it starts as 0. Which scripts call Save 93, and with what value, was not
traced. [V] for the writers, [I] for the value. Closed 2026-10-28: no shipped script calls Save 93,
so the byte stays 0 (`INTERNATIONAL_CONTENT.md` 3). [V]

It gates two remaps: the battle root (section 6) and the dead localized event directory
substitution (section 7.2).

## 3. The path table (`cdrom.fnd`, `cdrom.fid`, `cdrom.def`)

Loaded at boot by `FFX_Asset_LoadIndexTables 0x642FC0` into asset loader slots 12 and 11. Format
and the resolver are in `CHEAT_WARP.md` 1.1 and 12.3. In short: `u32 count` (16,305), then
`count + 1` u32 offsets from the table base, then the strings. 2,548 slots are empty. `cdrom.fid`
is `s16[65]`, the first index of each kind, `-1` for unused. `cdrom.def` names the kinds
(`_cdindex_*`). [V]

The resolver `FFX_Asset_ResolvePathWithDebugOverrides 0xA42C00` expands `$USER$` (`sub_820620`,
`"sugimoto"` or `off_C494A8`), applies the 16 slot substitution table and three debug package
swaps (`nagi0000`, `kami0400`, `nagi0700`). The table is the same for every language, every path
in it is `host0:/ffx/master/jppc/...` or `host0:/ffx/proj/...`, and every read goes through
`FFX_File_OpenHost0`. [V]

Check result per kind (`ok` / not in the archive). Kinds with every entry present are folded into
the last row. [V by the checker]

| kind | `cdrom.def` name | entries | ok | missing, and why |
|---|---|---|---|---|
| 0 | (system) | 16 | 1 | PS2 boot files, `.irx`, `system.cnf` |
| 1 | ROOT | 1 | 0 | `SLPM_675.13`, the PS2 executable |
| 12 | event | 5,568 | 834 | 18 `.ebp` with a path but no file (`CHEAT_WARP.md` 1.3), 4,716 `.mgrp` motion groups the PC build never ships at these paths |
| 13 | battle | 28 | 27 | `country.txt` |
| 14 | battle_scene | 818 | 817 | the `$USER$` debug entry |
| 17 | effect | 1,024 | 0 | written `host0:magic/...` with no slash, so the 7 byte strip eats `host0:m` and leaves `/ffx_ps2/agic/...`. PC magic is DLLs, nothing reads these |
| 19 | map | 646 | 311 | 323 `mapout.txa` and 12 `mapout.vpa`. PC maps load from `.ahwin32` (5.9) |
| 21, 49 | movie, movie2 | 115 | 0 | PS2 `adpcm` movie data |
| 25, 27 | event_voice, battle_voice | 43 | 1 | PS2 `.pvs` voice banks, PC voice is FMOD |
| 26 | battle_map | 114 | 56 | `mapout.txa` and one `.vpa` |
| 28 to 34 | chr_pc .. chr_skl | 4,460 | 3,474 | 35 `.chr` and 951 `.mgrp`, mostly unused models and motion sets |
| 37 | menu2 | 19 | 16 | two `face_*.fmt`, `menu.fpd` |
| 38, 44, 45 | music, soundwave, soundmusic | 260 | 0 | PS2 sound, replaced by FMOD |
| 46 | event_bmotion | 2 | 1 | `regmot2.mgrp` |
| 3, 4, 7, 9, 11, 16, 18, 20, 24, 39 to 43, 47, 48 | | 643 | 643 | none |

## 4. Builders by family, and the remap rules inside them

Every row is [V] (read in Hex-Rays) unless marked.

| family | builder | template |
|---|---|---|
| kernel tables | `FFX_Btl_ReadKernelBin 0x782DF0` | `host0:/ffx/master/new_<tree>/battle/kernel/<name>.bin` via OpenHost0 |
| localized bin | `FFX_LoadLocalizedBin 0x88CAC0` | `/ffx_ps2/ffx/master/new_<tree>/` + `menu/<n>.dcp` if the name has `macro`, else `battle/btl/<n>/<n>.bin` if it has `_`, else `battle/kernel/<n>.bin` if it has `monster`, else `event/obj_ps3/<n><n from its first slash>.bin` |
| localized ftc | `FFX_LoadLocalizedFtc 0x88C860` | same prefix + `battle/btl/<n>/<n>.ftc` if `_`, `menu/base.ftc` if `base`, `menu/newkit.ftc` if `newkit`, `help/help.ftc` if `help`, else `event/obj_ps3/<n><tail>.ftc` |
| menu bin | `FFX_LoadLocalizedMenuBin 0x88CCF0` | same prefix + `menu/<n>.bin` |
| help screens | `sub_88CE80` | row of `byte_B5DF10` (0x100 stride) + `off_C58E44[i]` (screen) or `off_C58E58[i]` (page) |
| sjis table | `sub_646920` | `host0:/ffx/master/jppc/ffx_encoding/ffxsjistbl_<jp,kr,ch,us>.bin` |
| Al Bhed table | `sub_790340` | `/ffx_ps2/ffx/master/new_<krpc,chpc>/battle/kernel/albhed.bin`, other languages load nothing |
| skip prompt | `sub_638E90` | `PS3Data/menu/D3D11/skip_<code>.dds.phyre` |
| loc kit | `sub_6DB2F0` | `PS3Data/LocKit/FFX_LOC_KIT_PS3_<CODE>.BIN` |
| video list | `FFX_Fmv_ParseVideoList 0x6D82F0` | `PS3Data/Video/<JP,US,Asia>/FFX_VideoList.txt` by master, then each line `<id>:,<primary>,<alt>` becomes `<root>/FFX_Data/GameData/PS3Data/Video/<primary>` (the one caller passes 0 at 0x63650B, so the alt column is never used) |
| flash | `sub_6E85F0`, `sub_702A80` | `PS3Data/flash/BOX<"",_KR,_CH>.swf`, `PS3Data/flash/saveload_<jp,kr,ch>.swf` |
| menu textures | `sub_67C7A0`, tables `0xC334F0` .. `0xC335EC` (9 pointers each) | `PS3Data/<menu dir>/D3D11/{meswin,battle,icon,strtex}.dds.phyre` plus five shared `PS3Data/menu/D3D11/*` |
| font pages | `FFX_Font_LoadPageTextures 0x67C920` from `TOInit` via `sub_6422E0` | `PS3Data/<menu dir>/{base_ftc,newkit_ftc}/D3D11/{font,shadow}_0_{0,1}.dds.phyre` |
| help fonts | `sub_67CE50` | `PS3Data/<help,help_kr,help_ch>/help_ftc/D3D11/{font,shadow}_0_{0,1}.dds.phyre` |
| help monitors | `sub_67CE50`, tables `0xC59D88` and `0xC59DAC` | `PS3Data/<help dir>/{s_monitor,now_help}/D3D11/0.dds.phyre`, cached in `dword_25D16CC` and `dword_25D16C8` |
| mon_boku | `sub_67CBD0`, `sub_8AC8C0` | `PS3Data/<help dir>/mon_boku/D3D11/0.dds.phyre` |
| helpgra | `sub_774AE0` | `PS3Data/<help dir>/helpgra/GCM/help%d.dds.phyre` |
| event TexList | `sub_874C50` | `PS3Data/event/<prefix>obj/<xx>/<name>/tex/TexList.txt` |
| magic TexList | `sub_664D80` | `PS3Data/magic/<magic name>/tex[_<code2>]/TexList.txt` |
| magic texture | `FFX_Magic_DrawSpriteTex0 0x906420` | `tex_cn` for `magic_0697` and `magic_0698` when the language is 11. This is host 812, a 2D sprite draw used by 10 DLLs, not the magic texture loader. The ordinary magic textures are the TexList rows above, see `MAGIC_ASSETS.md` |
| map header | `sub_659290` | `PS3Data/<map dir>[/2d]/<last>.ahwin32`, map dir is `path + 23` up to `/bin` |
| field fp TexList | `sub_664F60` | `PS3Data` + gfx+64240 + `fp/tex/TexList.txt`. The gfx+64240 string was not traced, the checker assumes `/<map dir>/` [I] |
| voice maps | `FmodVoice__initList` (0x70AB90, `SOUND_CLASSES.md` section 6) | `PS3Data/Sound_PC/Voice/VoiceIDMapper.txt`, `PS3Data/Sound_PC/Voice/<US,JP>/VoiceFevMapper.txt` by the voice flag (FmodVoice+4), not the language |
| SyncData | `FFX_SyncData__requestForMap` | `/FFX_Data/GameData/%sData/SyncData/SyncData_%4.4d x5.txt`, no language |
| credits | `0x6DA550` (orphan) | `/ffx_ps2/ffx/master/new_<tree>/event/obj_psv/cr/credits/credits.bin` |
| ability map | `sub_A45E30` | `PS3Data/menu/abmap` + `/dat00/` or `/dat12/` + `tex/D3D11` + `/14080_19.dds.phyre`, no language |
| cosmetic RNG tables | `FFX_Rand_SeedCosmeticStreams` | `/ffx_ps2/ffx/yonishi_data/dat_et/et_ffx.bin`, `.../bat_eff/et_tex.bin` |
| sphere grid | `FFX_SphereGrid_InitNodes` | `/ffx_ps2/ffx/eiichi_abmap_data/...` |

Names passed to these builders, from the call sites:

- kernel: `command`, `ply_rom`, `a_ability`, `item`, `monmagic1`, `monmagic2`
  (`FFX_Btl_LoadKernelTable`), `ply_save` (`FFX_InitNewSaveData`,
  `FFX_SaveData__reloadCharNames`), `status_txt`, `arms_txt`, `btl_txt`, `btlend_txt`,
  `build_txt`, `config_txt`, `item_txt`, `menu_txt`, `mmain_txt`, `name_txt`, `save_txt`,
  `summon_txt` (`FFX_KernelString_LoadGroup`), `important` (`FFX_KeyItem_LoadTable`), `w_name` (`FFX_Equip_LoadNameTable`), `sphere`,
  `panel` (`FFX_SphereGrid_LoadPanelTables`, `FFX_SphereGrid_ReadPanelTables`). [V]
- localized bin: `monster1..3` (`FFX_Btl_OpenMonsterArchives`), `macrodic`
  (`FFX_MenuRes_LoadMacroDic`), the battle field name (`FFX_Btl_OpenFieldArchive`, the path table
  kind 14 entry with everything up to `/battle/btl/` and from the last slash removed), the event
  name (`FFX_Ev_LoadEventPackage`, `g_ffxCurrentEventName`, the kind 12 entry between
  `/event/obj/` and the last slash, for example `bl/bltz0000`). [V]
- localized ftc: the same battle and event names, `base` and `newkit` (`TOInit`), `help`
  (`sa_menu_start 0x770430`, sa_menu code, not a Movie syscall as an older name had it), `cr/credits`
  (orphan). [V]
- menu bin: `menumain` (`FFX_Module0_ExecBoot`), `albheddic` (`TOInit`). [V]

## 5. The language table

Each cell is the part of the template that changes. A blank cell means the same as `jp`. `-`
means nothing is loaded. Columns are the language id from 2.1. [V] for every cell, read from the
switch in the named builder.

| family | jp 0 | us 1 | fr 2 | sp 3 | de 4 | it 5 | kr 9 | ch 10 | cn 11 |
|---|---|---|---|---|---|---|---|---|---|
| master version | 0 | 1 | 1 | 1 | 1 | 1 | 3 | 3 | unset |
| `<tree>` (kernel, bin, ftc, menu bin) | jppc | uspc | frpc | sppc | depc | itpc | krpc | chpc | uspc |
| help screen row | new_jppc | new_uspc | new_frpc | new_sppc | new_depc | new_itpc | new_krpc | new_chpc | new_uspc |
| base.ftc (TOInit) | new_jppc/menu/base.ftc | `ffx_ps2/ffx/master/uspc/menu/base.ftc`, path table kind 4 entry 0 | same as us | same as us | same as us | same as us | new_krpc | new_chpc | same as us |
| newkit.ftc (TOInit) | new_jppc | - | - | - | - | - | new_krpc | new_chpc | - |
| sjis table | ffxsjistbl_jp | ffxsjistbl_us | us | us | us | us | ffxsjistbl_kr | ffxsjistbl_ch | us |
| albhed.bin | - | - | - | - | - | - | new_krpc | new_chpc | - |
| skip prompt | skip_jp | skip_us | skip_fr | skip_sp | skip_de | skip_it | skip_kr | skip_ch | null |
| loc kit | _JP | _US | _FR | _SP | _DE | _IT | _KR | _CH | bare dir, open fails |
| video list | Video/JP | Video/US | Video/US | Video/US | Video/US | Video/US | Video/Asia | Video/Asia | none |
| flash BOX | BOX.swf | BOX.swf | BOX.swf | BOX.swf | BOX.swf | BOX.swf | BOX_KR | BOX_CH | BOX.swf |
| flash saveload | saveload_jp | jp | jp | jp | jp | jp | saveload_kr | saveload_ch | jp |
| menu dir (menu textures) | menu | menu_us | menu_fr | menu_es | menu_de | menu_it | menu_kr | menu_ch | menu_us (default row) |
| font pages | menu base+newkit | menu_us base | menu_us base | menu_us base | menu_us base | menu_us base | menu_kr base+newkit | menu_ch base+newkit | menu_us base |
| help fonts | help | - | - | - | - | - | help_kr | help_ch | - |
| s_monitor | help | help_us | help_fr | help_es | help_de | help_it | help_kr | help_ch | null pointer enqueued |
| now_help | help | help_us | help_fr | help_es | help_de | help_it | **help** | help_ch | null pointer enqueued |
| mon_boku | help | help_us | help_fr | help_es | help_de | help_it | help_kr | help_ch | help |
| helpgra | help | help_us | help_fr | help_es | help_de | help_it | help_kr | help_ch | Format left uninitialised |
| event TexList prefix (non Blitzball) | "" | "" | fr/ | sp/ | de/ | it/ | "" | "" | empty dir, `event//tex` |
| event TexList prefix (bltz0000, 0005, 0006, 0200, 0201) | "" | us/ | fr/ | sp/ | de/ | it/ | kr/ | ch/ | us/ |
| magic TexList (ids 7, 8, 32, 40, 46, 48, 50, 405) | tex | tex_us | tex_fr | tex_es | tex_de | tex_it | tex | tex | tex_jp |
| hiku21 map rename | hiku21 | hiku22 | hiku24 | hiku26 | hiku23 | hiku25 | hiku29 | hiku28 | hiku30 |
| battle root (text language gate, 6) | battle/new_btl | broken | broken | broken | broken | broken | battle/btl_kr | battle/btl_ch | broken |
| event root (6) | event/new_obj | broken | broken | broken | broken | broken | event/obj_kr | event/obj_ch | broken |

Notes on the table:

- **Korean `now_help` uses the Japanese directory.** `sub_67CE50` folds case 9 into case 0, while
  every other help family and `sub_8AC8C0` use `help_kr`. Both files exist, so nothing fails, the
  Korean player just sees the Japanese image if the two differ. [V]
- **The magic TexList ignores the language for jp, kr and ch** (always `tex`), and **language 11
  asks for `tex_jp`**, which does not exist. `sub_8AC300` has no case 11. [V]
- **Event TexLists outside the five Blitzball packages** take `""`, `fr/`, `sp/`, `de/`, `it/`
  from `off_C52768`, and events whose name contains `sins05` always take index 0. Every other
  `bltz` event takes the bltz branch but matches none of the five names, so it keeps the
  Japanese path. `event/es/obj/hi/hiku2100` ships but nothing builds `es/`. [V]
- **The font pages and the text tables are not the same switch.** `TOInit` loads `base.ftc` and
  `newkit.ftc` through `FFX_LoadLocalizedFtc` only for 0, 9 and 10. Every other language reads path
  table kind 4 entry 0 (`host0:/ffx/master/uspc/menu/base.ftc`) and no newkit. [V]
- **cn (11) is dead on PC.** Many families have a row for it, and `help_cn`, `menu_cn`, `tex_cn`,
  `ffxsjistbl_cn`, `Video/cn` exist in the archive, but no settings value reaches 11. [V]

## 6. The battle and event root remaps

`FFX_Path_RemapBattleHost0 0x8AD780` writes `byte_25D06A0`, `FFX_Path_RemapEventObjHost0
0x8AD840` writes `byte_25D08A0`. Both take a path table string and switch on the PC language: [V]

| language | battle: replace `host0:/ffx/master/jppc/battle/btl` with | event: replace `host0:/ffx/master/jppc/event/obj` with |
|---|---|---|
| 0 | `/FFX_Data/GameData/PS3Data/battle/new_btl` | `/FFX_Data/GameData/PS3Data/event/new_obj` |
| 9 | `.../battle/btl_kr` | `.../event/obj_kr` |
| 10 | `.../battle/btl_ch` | `.../event/obj_ch` |
| default | `host0:/ffx/master/uspc` -> `/FFX_Data/GameData/PS3Data`, which matches nothing in a `jppc` path | same |

then unify and cut after the last slash. For the default case the input still starts `host0:`, so
the result is `../../..host0:/ffx/master/jppc/battle/btl/<name>/` and every file under it looks
up `ost0:/...`. [V]

What these roots are for: every file under `event/new_obj`, `obj_kr` and `obj_ch` (1,120 under
`new_obj`) is a `font/D3D11/{font,shadow}_0_{0,1}.dds.phyre` page, the glyph images for that
event's or battle's own `.ftc`. [V by listing]

Who reaches them:

- **Event root.** `FFX_Ev_LoadEventPackage` calls the remap and `sub_642300(0, byte_25D08A0)`
  only when `FFX_LoadLocalizedFtc(event name)` returned non null. Event `.ftc` files ship only for
  jp (272 of 348 event names), kr (97) and ch (278), so **the broken default root is never
  reached for events.** No event that has its `.ftc` lacks its font directory. A few have a font
  directory and no `.ftc` (jp 1, kr 9, ch 10), which is harmless. [V]
- **Battle root.** `FFX_Btl_StepSubPhase` case 5 (0x783130) always calls the remap and then
  `sub_642300(8, byte_25D06A0)` **only when `FFX_MesWin_GetTextLanguage() == 0`**. That is the
  PS2 text language of 2.2, not the PC language. If it is 0 for every PC language, then us, fr,
  sp, de and it install the broken root as texture root 8 for every battle. Nothing then reads a
  font page from it, because no battle `.ftc` ships for those languages, so the effect is a
  dangling root, not a missing glyph. If it is not 0 for kr or ch, then their `btl_kr` and
  `btl_ch` pages are never installed. The value per build is the open question. [V] for the code,
  [I] for the consequence.

## 7. Other remaps

### 7.1 hiku21

`FFX_Gfx_FieldMapLoad 0x640210` rewrites `hiku21` in the map-out path in place before
`FFX_Gfx_BeginMapLoad` (the table in 5). All nine target maps ship, including the unreachable
`hiku30`. [V]

### 7.2 The localized event directory substitution, dead in retail

`FFX_Ev_SetLocalizedEventDirForMode 0x879F00` registers a substitution through
`FFX_Ev_RegisterLocalizedEventDirSubstitution 0x879430`: key `/ffx/proj/event/<xx>/` by text
language (jp, us, fr, es, de, it, uk 18), value by mode (`sub_873AA0`, `byte_132711F`, `-1`
reads as 1): `/ffx/proj/event/map/<xx>/`, `/ffx/master/<xx>pc/event/`, `/ffx/proj/event/sound/`,
`/motion/`, `/light/` variants. Both callers inside reachable code, `FFX_Ev_LoadEventPackage`
0x872FB2 and the `sub_8799B0` / `sub_879A70` pair at 0x87331C, sit under `if (sub_887D10())`,
and `sub_887D10` returns 0. The three other call sites (0x8795A1, 0x879737, 0x8798AC) are in code
with no reference. So the substitution table stays empty in retail, and the keys would match
only one path table entry anyway. [V]

### 7.3 `$USER$` and the debug swaps

Section 3. Only one path table entry uses `$USER$` (kind 14, a dev output file). [V]

## 8. Check result (2026-10-13 run)

54,003 rows: 19,781 found, 25,723 absent but tolerated (the caller handles null, or it is a
directory that only some entries have), 512 not loaded for that language, 7,987 missing. Of the
missing, 7,576 are path table PS2 leftovers (section 3), 88 + 237 are map headers for maps the
path table lists but the PC build does not ship (test maps, `titl00..02`, `zzzz*`, and so on),
37 are FFX.exe literals, 18 are the `test_proj` help screen, and the last 31 are the event and
magic TexLists and the cn loc kit below. [V by the checker]

Per family, `found/absent-ok/MISSING/not loaded`. Path table rows are in section 3.

| family | jp | us | fr | sp | de | it | kr | ch | cn |
|---|---|---|---|---|---|---|---|---|---|
| kernel | 23/0/0/0 | 23/0/0/0 | 23/0/0/0 | 23/0/0/0 | 23/0/0/0 | 23/0/0/0 | 23/0/0/0 | 23/0/0/0 | 23/0/0/0 |
| localized bin | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 |
| localized ftc | 4/0/0/0 | 1/2/0/1 | 1/2/0/1 | 2/1/0/1 | 1/2/0/1 | 1/2/0/1 | 4/0/0/0 | 4/0/0/0 | 1/2/0/1 |
| menu bin | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 |
| event bin | 308/40/0/0 | 308/40/0/0 | 308/40/0/0 | 308/40/0/0 | 308/40/0/0 | 309/39/0/0 | 308/40/0/0 | 308/40/0/0 | 308/40/0/0 |
| event ftc | 272/76/0/0 | 0/348/0/0 | 0/348/0/0 | 0/348/0/0 | 0/348/0/0 | 0/348/0/0 | 97/251/0/0 | 278/70/0/0 | 0/348/0/0 |
| battle bin | 155/662/0/0 | 155/662/0/0 | 155/662/0/0 | 155/662/0/0 | 155/662/0/0 | 155/662/0/0 | 155/662/0/0 | 155/662/0/0 | 155/662/0/0 |
| battle ftc | 96/721/0/0 | 0/817/0/0 | 0/817/0/0 | 0/817/0/0 | 0/817/0/0 | 0/817/0/0 | 19/798/0/0 | 51/766/0/0 | 0/817/0/0 |
| help screen | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 |
| help page | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 | 4/0/1/0 |
| sjis table | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 |
| albhed | 0/0/0/1 | 0/0/0/1 | 0/0/0/1 | 0/0/0/1 | 0/0/0/1 | 0/0/0/1 | 1/0/0/0 | 1/0/0/0 | 0/0/0/1 |
| skip texture | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 0/0/0/1 |
| lockit | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 0/0/1/0 |
| flash box, saveload | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 | 2/0/0/0 |
| video list | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 1/0/0/0 | 0/0/0/1 |
| videos (primary column) | 61 | 61 | 61 | 61 | 61 | 61 | 61 | 61 | - |
| menu textures (local + shared) | 9/0/0/0 | 9/0/0/0 | 9/0/0/0 | 9/0/0/0 | 9/0/0/0 | 9/0/0/0 | 9/0/0/0 | 9/0/0/0 | 6/0/0/0 |
| font pages | 8/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 4/0/0/0 | 8/0/0/0 | 8/0/0/0 | 4/0/0/0 |
| help fonts | 4/0/0/0 | 0/0/0/1 | 0/0/0/1 | 0/0/0/1 | 0/0/0/1 | 0/0/0/1 | 4/0/0/0 | 4/0/0/0 | 0/0/0/1 |
| s_monitor, now_help, mon_boku | 3/0/0/0 | 3/0/0/0 | 3/0/0/0 | 3/0/0/0 | 3/0/0/0 | 3/0/0/0 | 3/0/0/0 | 3/0/0/0 | 1/0/0/2 |
| helpgra | 29/0/0/0 | 29/0/0/0 | 29/0/0/0 | 29/0/0/0 | 29/0/0/0 | 29/0/0/0 | 29/0/0/0 | 29/0/0/0 | 0/0/0/1 |
| event TexList | 17/0/0/0 | 17/0/0/0 | 15/0/2/0 | 14/0/3/0 | 15/0/2/0 | 15/0/2/0 | 17/0/0/0 | 17/0/0/0 | 9/0/8/0 |
| magic TexList | 577/0/0/0 | 576/0/1/0 | 576/0/1/0 | 576/0/1/0 | 576/0/1/0 | 576/0/1/0 | 577/0/0/0 | 577/0/0/0 | 569/0/8/0 |
| map header, 3D section | 292/0/88/0 | hiku22 ok | hiku24 ok | hiku26 ok | hiku23 ok | hiku25 ok | hiku29 ok | hiku28 ok | hiku30 ok |
| map header, 2d section | 143/0/237/0 | ok | ok | ok | ok | ok | ok | ok | ok |
| battle root | 77/740/0/0 | 0/817 | 0/817 | 0/817 | 0/817 | 0/817 | 9/808 | 86/731 | 0/817 |
| event root | 273/75/0/0 | 0/348 | 0/348 | 0/348 | 0/348 | 0/348 | 106/242 | 288/60 | 0/348 |
| voice maps (all) | 3/0/0/0 | | | | | | | | |
| FFX.exe literal files (all) | 384/0/32/0 | | | | | | | | |
| FFX.exe literal dirs (all) | 35/0/5/0 | | | | | | | | |

**The real gaps on reachable languages:**

1. **`magic_0048` has no localized TexList.** It is in the `sub_65EFD0` list (ids 7, 8, 32, 40, 46,
   48, 50, 405), so us, fr, sp, de and it ask for `magic/magic_0048/tex_<code2>/TexList.txt`,
   and only `tex/` ships. `sub_664550` treats a missing TexList as done: it sets gfx+36 = 1 and
   returns 5, so in those five languages none of `magic_0048`'s textures load and the effect starts
   without them (2026-10-27, `MAGIC_ASSETS.md` section 7). [V] for the miss and the code.
2. **Event TexLists `lc/lchb0803` and `lc/lchb0806`** are missing for fr, sp, de and it (the
   `off_C52768` prefix sends them to `event/<xx>/obj/`), and **`hi/hiku0801`** is missing for sp.
   These are only requested by event scripts through ATEL `Core_282` / `Core_283`, so the miss
   only matters if those events make the call. [V] for the miss, [I] for whether it is reached.
3. **`help/test_proj` screen and page** are missing for every language. Screen 0 is the PS2 dev
   test screen. [V] for the miss, [I] that nothing asks for index 0.

Everything else that misses is either the unreachable cn column, a PS2 leftover in the path table,
or a map the PC build does not ship. The 32 missing FFX.exe literals are `menu_cn/*`, `help_cn`,
`menu/subfont`, `menu/titl/GCM/TexList.txt` (the dead titl TexList), two
`event/obj/zn/znkd13xx/font` pages, one `map/hiku/hiku08/fp/tex/GCM/...` texture,
`event/obj/cr/credits/credits.ebp` and `sc/scene7/scene700.mgrp`. The 5 missing directories are
`menu_cn/abmap`, `saves/`, `new_cnpc`, and two bare `new_` prefixes that get a suffix appended.

## 9. Proposed IDB names

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x6DB090 | `sub_6DB090` | `FFX_LangMgr_InitFromSettings` | the settings to master and language switch in 2.1 |
| 0x6DB070 | `sub_6DB070` | `FFX_LangMgr_Get` | returns `dword_CDED58`, read by every language switch |
| 0x6DAFC0 | `sub_6DAFC0` | `FFX_LangMgr_Create` | allocates and constructs `dword_CDED58` |
| 0x6DB2F0 | `sub_6DB2F0` | `FFX_LangMgr_LoadLocKit` | builds and opens `FFX_LOC_KIT_PS3_<CODE>.BIN` |
| 0x646920 | `sub_646920` | `FFX_Text_LoadSjisTable` | builds `ffxsjistbl_<code>.bin` |
| 0x638E90 | `sub_638E90` | `FFX_Fmv_GetSkipTexturePath` | returns `skip_<code>.dds.phyre`, asserts in `SpecialFeatureDisplay.cpp` |
| 0x790340 | `sub_790340` | `FFX_Btl_LoadAlbhedLocalized` | loads `albhed.bin` for kr and ch only |
| 0x88CE80 | `sub_88CE80` | `FFX_LoadLocalizedHelpScreen` | `byte_B5DF10` rows plus the screen or page suffix |
| 0x6E85F0 | `sub_6E85F0` | `FFX_Flash_LoadBox` | `BOX%s.swf` |
| 0x702A80 | `sub_702A80` | `FFX_Flash_LoadSaveLoad` | `saveload_<jp,kr,ch>.swf`, sets the AS3 language |
| 0x774AE0 | `sub_774AE0` | `FFX_Help_BuildHelpgraPath` | `helpgra/GCM/help%d.dds.phyre` by language |
| 0x8AC300 | `sub_8AC300` | `FFX_GetLanguageCode2` | `us fr es de it kr ch uk`, default `jp` |
| 0x8AC8C0 | `sub_8AC8C0` | `FFX_Tex_ResolvePs2TexturePath` | maps PS2 texture ids to the per language PC textures |
| 0x67C7A0 | `sub_67C7A0` | `FFX_SysTex_LoadMenuTextures` | picks one of the `0xC334F0` .. `0xC335EC` tables |
| 0x67CE50 | `sub_67CE50` | `FFX_SysTex_LoadHelpTextures` | help fonts, `s_monitor`, `now_help` |
| 0x65EFD0 | `sub_65EFD0` | `FFX_Magic_HasLocalizedTextures` | true for 7, 8, 32, 40, 46, 48, 50, 405 |
| 0x659290 | `sub_659290` | `FFX_Gfx_BuildMapHeaderPath` | see `ASYNC_LOAD_REQUESTS.md` |
| 0x874C50 | `sub_874C50` | `FFX_Ev_BuildTexListPath` | see `ASYNC_LOAD_REQUESTS.md` |
| 0x664D80 | `sub_664D80` | `FFX_Magic_EnqueueTexList` | see `ASYNC_LOAD_REQUESTS.md` |
| 0x664F60 | `sub_664F60` | `FFX_Map_EnqueueFpTexList` | see `ASYNC_LOAD_REQUESTS.md` |
| 0x887D10 | `sub_887D10` | `FFX_IsDevHarness` | returns 0, gates about 40 dev branches including 7.2 |
| 0x873AA0 | `sub_873AA0` | `FFX_Ev_GetLocalizedDirMode` | `byte_132711F`, `-1` reads as 1 |
| 0x8795B0 | `sub_8795B0` | `FFX_Ev_BuildLocalizedDevPath` | calls `FFX_Ev_SetLocalizedEventDirForMode`, every caller dead |

## 10. Corrections

- `CHEAT_WARP.md` 12.1 says the game itself uses one substitution slot through
  `FFX_Ev_SetLocalizedEventDirForMode`. In retail it uses none, see 7.2. A dated note is appended
  there.
- Project Zanarkand `PROGRESS.md` 4.4 says only Japanese reads the battle root. The gate is the
  PS2 text language being 0, not the PC language being Japanese (6). The event root is reached
  only when the event `.ftc` loads, which is jp, kr and ch. Not edited here, it is in the report.
- `Paths.cpp` in the port builds `LocalizedFtcPath("base")` for any language. The game asks for it
  only for jp, kr and ch and uses path table kind 4 entry 0 otherwise (5). Not a wrong path, a
  wrong caller. Reported, not edited.
