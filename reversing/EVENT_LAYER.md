# The event layer: `FFX_Ev_*` and `FFX_EvScript_*`, what sits between the game loop and the VM

Written 2026-10-26 for Project Zanarkand PROGRESS 1.8.13. Every address is an IDA VA in FFX.exe
(image base 0x400000, subtract it for a kit RVA). `[V]` means read in Hex-Rays or the disassembly
this session (idalib, scratch copy of the IDB, read only), `[I]` means inferred from names, shape
or call sites and not read end to end. Nothing in the IDB was renamed. Proposed names are in
section 9 and in the scratchpad file `evlayer_names.py`.

Read with `MAP_TRANSITIONS.md` (sections 2 and 3 are the full teardown and rebuild, this doc does
not repeat their tables), `ATEL_CONTEXT.md` (the package header, the pool, the interpreter),
`EBP_FORMAT.md` (the container), `ATEL_CH_LIBRARY.md` (where the `FFX_EvScript_*` bodies belong),
`ASSET_PATHS.md` 7.2 (the dead directory substitution) and `CHEAT_WARP.md` 5 and 12 (the warp API
and the path table).

## 0. The short answer

- **`FFX_EvScript_*` is not a layer.** The nine functions are bodies of the ATEL `ch` syscall
  library (lib 5) that an early pass named from their log strings. Eight are table slots (ch:12,
  23, 31, 37, 40, 92, 93, 94) and one, `FFX_EvScript_chSetMotion`, is the shared helper behind
  ch:25, 26, 65..70, 137..140. They add nothing on top of the VM, they are the VM. [V]
- **`FFX_Ev_*` is the per map event package lifetime.** Sixteen functions: one loader with one
  caller (`FFX_Ev_LoadEventPackage`, called only by `maybe_FFX_Scene_Init`), the image read, the
  boot scene hardcodes for events 393..399, two unload helpers, the background map load, a
  resident CHR list getter, the event TexList path and a dead localized directory substitution.
  Three of the sixteen are generic helpers that only got an `Ev` prefix because the loader was
  the first caller anyone read, and their names are now misleading (section 8). [V]
- **There is no event scheduler.** Event scripts are not scheduled by this layer. They run inside
  the VM's own context step (`FFX_Atel_StepOnce` -> `FFX_Atel_StepFieldAll` ->
  `FFX_Atel_StepContextRange(0, 6)`), once per sub-step of `FFX_MainStep`. The event layer only
  runs when a map change is consumed at the top of that same `StepOnce`, plus one small per step
  piece, the EvTex sprite animation (section 4). [V]
- **The layer is single slot.** One package, one actor pool, one localized bin, one localized ftc,
  one event name, one event id. The source had a two slot design (arrays at 0x23CBD68..0x23CBD7C)
  and its per slot helpers survive as unreferenced code, retail only ever uses slot 0. [V]
- **Lockstep: almost clean.** No wall clock, no RNG, no input. The disk reads are blocking, so
  they cost wall time and never a step. What a port has to watch is the language driven files
  (message tables and font), raw heap addresses, the uncleared pool, and the fact that the title's
  load runs on a JobWorker thread at boot. Section 7.

---

## 1. Every function, by role

### 1.1 Package lifetime (6)

| VA | name | what it does | callers | mark |
|---|---|---|---|---|
| 0x872EF0 | `FFX_Ev_LoadEventPackage` | `int (int eventId)`. The whole rebuild after the unload, section 3 | `maybe_FFX_Scene_Init` 0x88E158 only | [V] |
| 0x88D2C0 | `FFX_Ev_ReadEventPackageImage` | `(kind 12, 18 * id)`: loader slot 4 selects the kind, slot 5 gives the size (a boot scene takes `FFX_Ev_GetBootPackageSize` instead), `FFX_Mem_SetNextAllocAlign(64)`, `FFX_MemAlloc` rounded up to 2048, `*image = 0`, slot 0 reads with tags "EVENT READ" / "EVENT MALLOC DONE", slot 2(0) pumps to completion ("EVENT READ SYNC"). Returns the image | `FFX_Ev_LoadEventPackage` 0x872FC3 | [V] |
| 0x88E650 | `FFX_Ev_AllocActorPool` | `FFX_Mem_SetNextAllocAlign(64)` then `FFX_MemAlloc((n + 2047) & ~2047)`. Not cleared. **A generic 2 KB rounded tklib allocator**, not an actor pool function | `FFX_Ev_LoadEventPackage`, `FFX_Btl_ReadKernelBin`, Save 21 (dead), Save 84, `sub_646920`, `sub_790430`, the orphan slot helper at 0x872A40 | [V] |
| 0x88D1C0 | `FFX_Ev_PumpReadsBlocking` | `jmp FFX_GetAssetLoader()[2]` with the caller's argument still on the stack, so it is `FFX_RomDev_Pump(mode)`. Mode 0 blocks until every ROM read is done and dispatched, mode 1 is a poll that returns 1 while reads remain. **Only 3 of the 16 call sites pass 0** | blocking: `FFX_Ev_LoadEventPackage` 0x8733E6 and 0x8734E0, `FFX_AtelSys_Core_573_BeginNewGame`. Polling (1): `FFX_Module0_ExecBoot` x3, `FFX_MenuSys_AnythingStillRunning`, `FFX_Menu_ExecModule12Shop` x4, `sub_8D9600` x3, `sub_887020`, `FFX_MenuTest_ExecTK` | [V] |
| 0x86DD30 | `FFX_Ev_LoadSceneBackground` | `FFX_SaveData_SetSceneAndSub(m, 0)`, then `FFX_Map_LoadFieldMapSync(m, 0)` when `m > 0` (group 19 map out, synchronous) | `FFX_Ev_LoadEventPackage` 0x873463 (RES flag bit 0 set), Save 25 (no shipped caller) | [V] |
| 0x86DCA0 | `FFX_Ev_LoadSceneBackgroundAlt` | **byte for byte the same instructions as the one above.** The RES flag bit 0 choice in the loader therefore changes nothing in retail | `FFX_Ev_LoadEventPackage` 0x87346A, Save 3, `FFX_Vfx_PumpCommandRing` case 23 (the PMCOM debug protocol ring, dev only) | [V] |

A third copy with no function and no reference sits at 0x86DD00. It takes a flag and picks
`FFX_Map_LoadMapSync` (group 26) or `FFX_Map_LoadFieldMapSync` (group 19). Dead. [V]

### 1.2 Unload side (3)

| VA | name | what it does | callers | mark |
|---|---|---|---|---|
| 0x8742B0 | `FFX_Ev_DestroyEventModels` | Resets the **128 EvTex sprite slots** at `word_1327148` (stride 28: word 0 = 0x8000, visible 0, scale 0x100/0x100, colour 0x80808080), destroys the six dyn geo groups keyed per slot (`key = slot + 10000 * k`), releases the event TexList when `dword_1327140`, then zeroes the sprite buffer globals. It is not a model destructor, section 4 | `maybe_FFX_Map_Unload` 0x872D32 (only while context 0 is active), the thunk below | [V] |
| 0x874830 | `j_FFX_Ev_DestroyEventModels` | thunk | `FFX_Atel_Init` 0x86D73F | [V] |
| 0x874130 | `FFX_Ev_ReleaseSaveE0Buffers` | Reads save `+0xE0`, which is the **premovie state** (Core 432 sets 1, 433 sets 6, 437 moves 2..4 to 5, 449 sets 4). States 1, 2, 3, 5 return untouched, so a premovie that is loading or ready **survives the map change**. State 4: a print stub. Anything else: frees `dword_1327110/14/18` and zeroes save `+0xE0`. Those three buffers have **no writer other than zeroing** anywhere in the binary, so on PC the frees never fire and the function reduces to "zero +0xE0 unless 1, 2, 3, 5" | `maybe_FFX_Map_Unload` 0x872ED2 | [V] |

### 1.3 The boot scene window (3)

| VA | name | what it does | callers | mark |
|---|---|---|---|---|
| 0x67AB50 | `FFX_Ev_IsBootSceneEvent` | `(unsigned)(id - g_ffxFirstBootSceneEventId) <= 6`, so 393..399 | 21 sites. With `g_ffxCurrentEventId`: `FFXApplication__animate` (copyright texture), `FFX_MainStep` 0x8215A7 (quit phase), `FFX_Input__poll` (zeroes the button mask), `FfxFmod_QueueCommand` (kicks the audio thread for commands 8 and 58), the esc menu popups `sub_6FC7B0`, `sub_6FC9B0`, orphan 0x6FC795. With `g_ffxLiveSceneId`: `FFX_Fmv_DestroyPlayer`, `FFX_MesWin_OnConfirmPressed`, `FFX_RomDev_EnqueueRead`, `sub_8DE5D0`. With its argument: `maybe_FFX_Scene_Init` x2, `FFX_Ev_LoadEventPackage` x2, `FFX_Ev_ReadEventPackageImage`, `sub_76C320`, `FFX_RomDev_EnqueueRead`, orphan 0x88EC96. With save `+0x00`: `sub_86BAE0` (menu open gate) | [V] |
| 0x67AAC0 | `FFX_Ev_GetBootPackagePath` | `off_C334B8[id - 393]`, `host0:/ffx/master/jppc/event/obj/sc/sceneN/sceneN.ebp`, null outside the window | `FFX_Ev_LoadEventPackage` 0x872FEA (the name) and 0x87328B (the font root), `FFX_RomDev_EnqueueRead` 0x76BBC0 (path fallback for sub-index 0) | [V] |
| 0x67AAE0 | `FFX_Ev_GetBootPackageSize` | Cached in `dword_C33484[7]`, initial -1. On the first ask it opens the host0 file (`sub_67A9F0`, "failed to open EternalCalm data file") and stores the size rounded up to 16 | `FFX_Ev_ReadEventPackageImage` 0x88D2FF, `sub_76C320` 0x76C397 (the size getter for sub-index 0) | [V] |

The unnamed twins for sub-index 3 (the boot scene `.mgrp`, `sceneN00.mgrp`) are `sub_67AA50` (path,
`off_C334D4`) and `sub_67AA70` (size, cache `dword_C334A0`), plus `sub_67AB30`, a second copy of
`FFX_Ev_IsBootSceneEvent`. **The `.mgrp` size cache has six slots, not seven.** In .data the order
is `C33484` ebp sizes (7 dwords), `C334A0` mgrp sizes, `C334B8` ebp paths. So `dword_C334A0[6]`, the
slot for event 399, is `off_C334B8[0]`, the pointer to the scene1 path string (0xB44E64). It is
positive, so `sub_67AA70(399)` never measures the file and returns 11,816,548 as the size of
`scene700.mgrp`. It is only reached when the path table gives no size for that row, through
`FFX_Ch_RomReadEventMotionGroup` and `sub_76C320`. [V layout and code, I whether a retail run ever
asks for 399's motion group]

### 1.4 Package queries (1)

| VA | name | what it does | callers | mark |
|---|---|---|---|---|
| 0x86AE20 | `FFX_Ev_GetResidentChrList` | `g_ffxAtelCtx0EventPkg + RES(+0x2C)` then RES `+40`, the u16 resident CHR id list. Falls back to the empty list `unk_1325B80` at any null step | `FFX_Ch_EventjumpRefreshCache` 0x83B674 | [V] |

### 1.5 Paths and language (3)

| VA | name | what it does | callers | mark |
|---|---|---|---|---|
| 0x874C50 | `FFX_Ev_BuildTexListPath` | `/FFX_Data/GameData/PS3Data/event/<prefix>obj/<group>/<name>/tex/TexList.txt`. Prefix from `off_C52768[FFX_GetLanguage()]` ("", fr/, sp/, de/, it/), index 0 for names containing `sins05`, and a per language directory for the five Blitzball names. Writes `off_C52768[11] = "cn/"` when the language is 11 and the event is 121 (language 11 is unreachable on PC, `ASSET_PATHS.md` 2.1) | `FFX_Ev_DestroyEventModels`, `sub_8748A0` (Core 283), `sub_874B70` (Core 282) | [V] |
| 0x879F00 | `FFX_Ev_SetLocalizedEventDirForMode` | Picks a `/ffx/proj/event/...` or `/ffx/master/<xx>pc/event/` directory by `sub_873AA0` (the mode byte `byte_132711F`) and the PS2 text language, registers it | `FFX_Ev_LoadEventPackage` 0x872FB2 **inside `if (FFX_IsDevHarness())`**, `sub_8795B0` (dead) | [V] dead in retail |
| 0x879430 | `FFX_Ev_RegisterLocalizedEventDirSubstitution` | Releases slot `dword_13285B4`, registers `/ffx/proj/event/<xx>/` -> the argument | 42 sites, all inside 0x879F00 | [V] dead in retail |

`FFX_IsDevHarness 0x887D10` is `return 0`. ASSET_PATHS 7.2 has the rest. [V]

### 1.6 `FFX_EvScript_*`, the misfiled `ch` syscalls (9)

Table base `g_ffxAtelSysFuncLib05_Ch 0xC891F8`, 16 byte rows {start, poll, float, int}. The row is
`(dref - base) / 16`, the slot is the remainder. [V]

| VA | name | row, slot | body | mark |
|---|---|---|---|---|
| 0xA78D30 | `FFX_EvScript_chEnGravity` | ch:12 int | pops one, prints the "already invalid, use chSetGravMode" warning, returns 0 | [V] |
| 0xA79130 | `FFX_EvScript_chSetGravMode` | ch:23 int | `FFX_Ch_SetGroundMode(chr, pop)` | [V] |
| 0xA79450 | `FFX_EvScript_chIsMotionActive` | ch:31 int | `CHR+0x736 != 0` | [V] |
| 0xA79670 | `FFX_EvScript_chSetMotionSlot` | ch:37 int | `FFX_Ch_SetSlot(chr, slot, value)` | [V] |
| 0xA799D0 | `FFX_EvScript_chSetRunThreshold` | ch:40 float | `FFX_Ch_SetRunThreshold(chr, popf)` | [V] |
| 0xA78D70 | `FFX_EvScript_chSetNextHokan` | ch:92 int | `FFX_Ch_SetNextHokan(chr, pop)` | [V] |
| 0xA78DD0 | `FFX_EvScript_chGetMotion` | ch:93 int | `FFX_Mot_GetMotionId(chr)` | [V] |
| 0xA78E20 | `FFX_EvScript_chGetMoveSpeed` | ch:94 float | `FFX_Ch_GetMoveSpeed(chr)` | [V] |
| 0xA7AAE0 | `FFX_EvScript_chSetMotion` | helper, 12 callers | pops five (plus an actor id when `a4`), patches `m_jointComposerMode` for hardcoded (scene, story progress, chr id, motion id, frame) tuples in scenes 205/2085, 364/1176, 17/200, sets `maybe_g_ffxC102SkinHackFlag` for chr 102, then mode 0 `FFX_Ch_SetMotionTbl`, 1 `FFX_Ch_SetMotionFrames`, 2 `FFX_Ch_SetMotionFramesInclusive` | [V] |

`ATEL_CH_LIBRARY.md` already documents them by row. Its apply script refuses to overwrite names an
earlier pass gave, which is why they kept the prefix. Section 8 proposes the scheme names.

**Counts.** 6 lifetime + 3 unload + 3 boot window + 1 query + 3 paths = 16 `FFX_Ev_*`, plus 9
`FFX_EvScript_*`, 25 in all.

---

## 2. The call path

### 2.1 From the step

```
FFX_MainStep 0x820AE0                                   main thread, once per presented step
  gameplay block (menus closed, save UI idle, no fade hold)
    for each sub-step:
      FFX_Player__stepControl, FFX_Btl_MainStep
      FFX_Atel_StepOnce 0x88D3D0                        call at 0x82101A
        if g_ffxMapChangePending and both countdowns are spent:
          FFX_App_ShowLoadingIndicator                  unless map 23, 348, or 268 at story 425
          maybe_FFX_Scene_Init(save, save.word[0]) 0x88E070
            ROM pump(0), scene id copies, FFX_Came_Init, message windows
            if g_ffxSceneLoaded: maybe_FFX_Map_Unload 0x872D20
              (ctx 0 active) FFX_Ev_DestroyEventModels, ... FFX_Atel_DeactivateContext(0)
              free g_ffxAtelActorPool, free g_ffxEvPackageBase
              FFX_Ev_ReleaseSaveE0Buffers, FFX_Map_UnloadField, FFX_Ch_EventjumpDispose
            magic preloads, VFX, FMV abort, colour tweaks
            FFX_Ev_LoadEventPackage(map) 0x872EF0       section 3
              FFX_Ev_ReadEventPackageImage              loader slot 0 read, slot 2(0) blocking pump
              maybe_FFX_Map_SetupChars 0x875510         ctx 0 reset, actors built, CHR pool
              FFX_Ev_LoadSceneBackground[Alt]           map out, synchronous
            title or checkpoint, pending = 0, loaded = 1, FMOD and SyncData requests
        FFX_Atel_StepFieldAll 0x872BD0                  always, the new map's first frame here
          pending zone apply (g_ffxPendingZoneMode 1 or 2)
          FFX_Atel_StepFieldContexts -> FFX_Atel_StepContextRange(0, 6)   THE VM
            per context: +0x4C callback -> InitFrame or StepFrame x3 -> FFX_Atel_RunScript
            then the boot task queue (FFX_BootTask_RunHead), CHRREG and friends
          FFX_Atel_StepFieldFrame                       encounters
          sub_8743E0 twice                              EvTex sprite animation, section 4
          sub_875B40/sub_875E10 -> pad port +255 (I: actuator levels)
          FFX_Field_StepMenuOpenRequest, FFX_MainStep_CheckQuit
        FFX_Voice_StepLoader
      ... cameras, CHR motion, player camera
  display list half
    FFX_Field_DrawEvTexSpritesAndStepVoice 0x88D4F0     call at 0x821435
      FFX_EvTex_DrawSprites 0x872C90                    draws the 128 slots on layer 8
```

[V] for every call named, from the bodies of `FFX_Atel_StepOnce`, `maybe_FFX_Scene_Init`,
`maybe_FFX_Map_Unload`, `FFX_Ev_LoadEventPackage` and `FFX_Atel_StepFieldAll`, and the call sites
listed by xref. `MAIN_LOOP.md` 2 has the sub-step loop, `ATEL_CONTEXT.md` 11.1 the VM side.

### 2.2 From the map change request

The request side owns nothing in this layer. A script syscall (Core 17, 171, 267, 268), the game
over path, the debug pages or `FFX_MainInit` call `FFX_Map_RequestChange`, which writes save
`+0x00/+0x02/+0x0C/+0x0D` and raises `g_ffxMapChangePending`. The only way into
`FFX_Ev_LoadEventPackage` is `maybe_FFX_Scene_Init`, and its callers are `FFX_Atel_StepOnce`
0x88D428 and `FFX_MainInit` 0x820A30 (debug mode, event 0) and 0x820A98 (retail, event 23 or 393
under `_ECalm`). The entry point index (save `+0x0C`) is consumed inside `maybe_FFX_Map_SetupChars`
by `FFX_Atel_StartEntryArrivalScript`, not by the event layer. `MAP_TRANSITIONS.md` 1 has the full
requester table. [V]

### 2.3 At boot

`FFX_Atel_Init 0x86D6D0` (inside `tklib__f88D710`, inside `FFX_MainInit`) does the layer's one time
init: `byte_132711F = 255` through `sub_873E40`, `sub_874FB0`, `j_FFX_Ev_DestroyEventModels`,
`sub_873CF0(0)` (no active ftc), `sub_873D00(-1)` (no current BGM), `sub_873C00` (premovie state 0),
and `__cfltcvt_init_90 0x873BA0`, which zeroes all six package and pool slots 0x23CBD68..0x23CBD7C and,
behind `FFX_IsDevHarness`, would init the dev preload table. Then `FFX_MainInit` loads the title
with `maybe_FFX_Scene_Init(save, 23)`. **All of that runs on a JobWorker thread**
(`BOOT_SEQUENCE.md` 11). [V]

---

## 3. `FFX_Ev_LoadEventPackage`, what it adds on top of the VM

`MAP_TRANSITIONS.md` 3.1 lists every global the loader touches. This is the same body read by role,
with the parts that table leaves out. [V unless marked]

1. **Identity.** `g_ffxCurrentEventId = id`. Single writer in the binary, 80-odd readers, most of
   them render and presentation code.
2. **Per map resets that do not belong to the VM.** Message windows, BGM pending, field boot mode,
   Al Bhed primer staging, scene id override, two sound groups, `dword_1326B5C/60/64`. These are the
   same calls `maybe_FFX_Map_Unload` makes, repeated so the title's first load (no unload before
   it) starts clean.
3. **Dev directory hook**, `if (FFX_IsDevHarness()) FFX_Ev_SetLocalizedEventDirForMode()`. Dead.
4. **Read.** `FFX_Ev_ReadEventPackageImage(12, 18 * id)`. The image pointer is also copied to
   `qword_13270F8` (only the low dword is used), **before** the magic check.
5. **Name.** The path from `FFX_Ev_GetBootPackagePath` (393..399) or
   `FFX_Asset_SelectKindAndGetPath(12, 18 * id)` is copied into `g_ffxCurrentEventName 0x23CBC60`,
   then cut to the text between `/event/obj/` and the last `/`, so the buffer holds
   `<group>/<name>` (for example `bl/bltz0000`). No null checks, `CHEAT_WARP.md` 12.4.
6. **Hardcoded per event text modes**, keyed by that name or the id: `g_ffxEvMesWinBlitzTextMode`
   (name contains `bltz` or `hiku2`, or id 23, 93, 291), `dword_1327104` (ids 79, 93, 97, 107, 110,
   136, 140, 259, 291, 329, read only by the message window placement `sub_8AF850`, where it lifts
   the x clamp for frame style 1), `g_ffxMesWinFontScale` 0.9 for `bltz`, else 1.0.
7. **Magic**, `if (image[0] != 'E' || image[1] != 'V') while (1);`.
8. **Install.** Free the old `g_ffxEvPackageBase` (already null after the unload), store the new.
9. **Per map encounter state.** `FFX_Field_ResetPerMapEncounterState(entryMapNo)`.
10. **Actor pool.** `FFX_Atel_CalcActorPoolSize`, free the old pool, `FFX_Ev_AllocActorPool`.
11. **Localized text.** Free and reload `dword_1327128` with `FFX_LoadLocalizedBin(name)`
    (`/ffx_ps2/ffx/master/new_<lang>pc/event/obj_ps3/<group>/<name>/<name>.bin`). When it exists,
    `g_ffxEvBuiltinMsgTable = image + image[+8]` and the context's table is the .bin. When it does
    not, the context's table is the package's own `+8` table. Then the default table from
    `image[+0x14]`. Free and reload `dword_132712C` with `FFX_LoadLocalizedFtc(name)`, and when it
    exists: `dword_1327120` = it, `FFX_Path_RemapEventObjHost0` writes the font page root
    `byte_25D08A0`, `sub_642300(0, byte_25D08A0, 0)`, `FFX_Font_InstallFtc`. Otherwise
    `dword_1327120 = 0`.
12. **Title front end.** Event 23 starts presentation stage 7. Any other id stops it when
    `g_ffxTitleScreenActive`, then clears that flag.
13. **Dev preload**, `if (FFX_IsDevHarness())`: `sub_8799B0` reads `debug/<name>.bdb`, `sub_879A70`
    reads up to 7 `.src` files, and up to 8 preload blocks are locked and handed to
    `maybe_FFX_Map_SetupChars` as its 6th argument. Dead. Retail calls
    `maybe_FFX_Map_SetupChars(pool, atel, msgs, default, 0, NULL, poolSize)`.
14. **Pump, BGM, SE, sound.** `FFX_Ev_PumpReadsBlocking(0)`, `FFX_Atel_LoadEventBgm`,
    `FFX_Atel_StartEventSe(atel, image + image[+0x0C], 1)`, `sub_886DD0(0)`, `sub_866210`.
15. **Background map.** `FFX_Ev_LoadSceneBackground[Alt](entryMapNo)`, synchronous map out read,
    then the gfx scene load goes to the AsyncLoadManager (`MAP_TRANSITIONS.md` 3.1).
16. **Zone and the rest.** `g_ffxPendingZoneMode = 0`, both pending zone ids -1, guide map layout
    preset 0 when `entryMapNo > 0`, sound groups, `FFX_Atel_LoadResourceList(atel, 1)`, two voice level
    bytes `byte_1327134 = 127` and `byte_1327135 = 63` (read by Core 214 StartVoice, written by
    Core 398..401), guide map on (`RCBG.md`), a final blocking pump.

So what the layer adds on top of the VM is: choosing and reading the file, the event name and the
hardcoded per event presentation switches derived from it, the localized message table and font,
the background map, the per map encounter reset, sound and BGM setup, and the reset of every
field side global that a map owns. The VM itself only starts in step 13 (`SetupChars` resets
context 0 and builds the actors) and first runs on the same `StepOnce`, in `StepFieldAll`.

---

## 4. The EvTex sprites, the one per step piece

The 128 slot table `word_1327148` (28 bytes per slot) is what `FFX_Ev_DestroyEventModels` resets.
It is the scripted 2D sprite system, written only by Core 282..288, 297, 298 and 575
(`ATEL_CORE_LIBRARY.md`). [V]

| part | VA | thread | what |
|---|---|---|---|
| init | `sub_874B70` (Core 282) | step | sprite buffer `dword_1327144`, end `dword_1327F48 = buf + size - 160`, image var `dword_23CBC5C`, draw enable `dword_1327F4C = 1`, and when the buffer is set the TexList enqueue (`sub_639310` -> `sub_663F00`, async kind 4), `dword_1327140 = 1` |
| slot writers | `sub_8748A0` 283, `sub_8747F0` 284, `sub_8749B0` 285, `sub_874AD0` 286, `sub_874B30` 287, `sub_874A80` 288, `sub_8749F0` 297, `sub_874970` 298, `sub_8749E0` 575 | step | every one clamps the slot to 0..127 |
| animation | `sub_8743E0` -> `sub_874BF0` | step, twice per `StepFieldAll` | for every active slot: frame timer `+24` += 256, at the frame's duration advance the frame, at the end wrap or clear the active byte. Reads only the sprite data in the script's buffer, never the texture. Skipped while a battle is pending or active and while 575 has disabled it |
| draw | `FFX_EvTex_DrawSprites 0x872C90` -> `sub_7111E0` -> `sub_7147B0` | step thread, display half | builds the slot's six dyn geo models on first draw, naming the texture `"%d_%d_%d.dds.phyre"` from `dword_23CBC5C - qword_13270F8` (the image var's offset inside the package) and the texture size bits. If the texture is not loaded yet it destroys the groups and tries again next draw. Scene 382 skips slots 5, 11, 17, 21 |
| teardown | `FFX_Ev_DestroyEventModels` | step | on unload when ctx 0 is active, and at boot |

The animation state is deterministic and step ordered. The texture's arrival is disk timed, but it
only decides whether a slot is drawn, never what the step computes. [V]

---

## 5. The state this layer owns

"Single slot" means one instance in the whole process, so two simultaneous events (or a second
peer's event in one process) cannot coexist. Every row is single slot unless marked.

| global | VA | written by | on a map change | notes |
|---|---|---|---|---|
| `g_ffxCurrentEventId` | 0x12FBC40 | `FFX_Ev_LoadEventPackage` | written | the "which event" key for 80-odd readers |
| `g_ffxLiveSceneId` | 0x1330844 | `maybe_FFX_Scene_Init`, before the unload | written | same value, one call earlier |
| `g_ffxCurrentEventName` | 0x23CBC60 | loader | written | `<group>/<name>`, read by the TexList builder, `sub_8E02F0`, `sub_91A830` |
| `g_ffxEvPackageBase` | 0x23CBD70 | loader, `maybe_FFX_Map_Unload`, Atel_Init | freed, rebuilt | slot 0 of a two slot array, slot 1 (0x23CBD74) is only ever zeroed. Holds ATEL classes 2, 3, 4, 6, so per map script state restarts |
| `g_ffxAtelActorPool` | 0x23CBD78 | same | freed, rebuilt | slot 0 of two (0x23CBD7C). **Not cleared** after allocation |
| `qword_13270F8` (low dword) | 0x13270F8 | loader | written on load, **not cleared on unload** | dangles between the unload and the next load, harmless because the only reader (`sub_7147B0`) needs EvTex state the unload has cleared |
| `dword_1327128` localized bin | 0x1327128 | loader only | freed and reloaded by the next load | **survives the unload**, freed only by the next load |
| `dword_132712C` localized ftc | 0x132712C | loader only | same | same |
| `dword_1327120` active ftc | 0x1327120 | loader, unload (0), Atel_Init, orphan 0x6DA6C9 | reset 0, then written | read by `FFX_Font_UploadFieldSheets` through `sub_873AC0` |
| `g_ffxEvBuiltinMsgTable` | 0x23CBD60 | loader | written only when a .bin exists, **else keeps the previous map's pointer** | read by `FFX_Atel_GetMessageText` as the fallback for a zero offset. A map without a .bin after one with a .bin keeps a pointer into the freed previous image [V write condition, I that a retail map ever lacks the .bin] |
| `byte_25D08A0` font page root | 0x25D08A0 | loader, only when an ftc exists | written or kept | read by `FFX_Font_ResolvePageTexture` |
| `g_ffxEvMesWinBlitzTextMode`, `dword_1327104`, `g_ffxMesWinFontScale` | 0x1327100, 0x1327104, 0xC5D784 | loader | written | presentation |
| `byte_1327134`, `byte_1327135` | | loader (127, 63), Core 398..401 | reset | voice levels read by Core 214 |
| `g_ffxTitleScreenActive` | 0x25D5EBC | loader (cleared off 23), Core 573, `sub_91A830` | reset off the title | |
| `g_ffxPendingZoneMode/Notify/Plain` | 0x132711C.. | loader resets, Core 393, MapFx 6 set, `StepFieldAll` and `FFX_Field_MapRegLoadTask` consume | reset | the deferred zone (walkmesh) switch |
| EvTex slots and buffer | `word_1327148`, `dword_1327140/44`, `dword_1327F48/4C`, `dword_23CBC5C` | section 4 | reset, **only when ctx 0 was active** | |
| premovie state | save `+0xE0`, `+0xD8`, `+0xDC` | Core 432, 433, 437, 449, `sub_873E90` | **survives** in states 1, 2, 3, 5, else reset 0 | `+0xD8/+0xDC` are never touched by the unload |
| premovie buffers | `dword_1327110/14/18` | only zeroed | always 0 on PC | PS2 leftovers |
| event dir mode | `byte_132711F` | Atel_Init 255, Save 9 (title and four dev packages), the debug screen | **survives** | read by `sub_873AA0` (255 reads as 1) for the dead substitution and for `sub_8827B0` |
| substitution slot | `dword_13285B4` | dead code | survives | always empty in retail |
| boot package size caches | `dword_C33484[7]`, `dword_C334A0[6]` | first use | **survive for the process** | measured from disk once. The second has the off-by-one in 1.3 |
| current BGM | `dword_1327130` | `sub_873D00` from 20 BGM syscalls | survives the event layer | it is the checkpoint's BGM word (save `+0xA8`), see 8.3 |

What the layer reads and does not own: `g_ffxAtelCtx0EventPkg` (context 0's package pointer,
`maybe_FFX_Map_SetupChars` writes it), the save block's map and entry words, the asset loader
table, `FFX_GetLanguage`, `FFX_MesWin_GetTextLanguage_impl`. [V]

---

## 6. What a recreation has to port, and what it can drop

### 6.1 Port

- The load sequence of section 3 minus the dev branches, in the same order relative to the VM:
  the unload frees the old package and pool, the load reads the new one, resets the field side
  globals, builds context 0, loads the background, and the first VM frame runs in the same
  `StepOnce`. Replace the `while (1)` with validation before the request (`CHEAT_WARP.md` 2.5).
- The event name derivation, because three things key off it: the localized bin and ftc file
  names, the Blitzball text mode and font scale, and the TexList path.
- The hardcoded per event switches (text mode ids, the 10 id clamp exemption, the title stage 7
  arm, the three loading indicator exceptions in `StepOnce`).
- The .bin present / absent rule for the message tables, including that the builtin table pointer
  is only written when the .bin exists. A port should reset it to the package's own `+8` table
  every load and record that as a deliberate difference.
- One background loader. The two `FFX_Ev_LoadSceneBackground*` bodies are identical.
- The boot scene window 393..399: the paths, the in-place size measurement, and the 21 readers'
  meaning (input gate, copyright, FMOD kick, quit). Use a seven entry size table for the `.mgrp`.
- The premovie rule on unload (states 1, 2, 3, 5 survive a map change).
- The EvTex sprite system: 128 slots, the per step animation inside `StepFieldAll` (twice per
  call), the draw in the display half, the TexList path.
- `FFX_Ev_GetResidentChrList` semantics, including the empty list fallback.
- The `FFX_EvScript_*` bodies, as ordinary `ch` syscalls (`ATEL_CH_LIBRARY.md`). Nothing extra.

### 6.2 Drop

- `FFX_Ev_SetLocalizedEventDirForMode`, `FFX_Ev_RegisterLocalizedEventDirSubstitution`,
  `sub_8795B0`, and the mode byte's only live reader beyond them, if `sub_8827B0` turns out to be
  dev only (not checked). All gated by `FFX_IsDevHarness` or unreferenced.
- The dev preload (`sub_8799B0`, `sub_879A70`, `sub_879820/840/860`, `sub_879E70`,
  `maybe_FFX_TkHarness_Init_DEAD`) and the 6th and 7th arguments of `maybe_FFX_Map_SetupChars`.
- The second package and pool slot and the four orphan slot helpers (0x872A40 realloc pool slot,
  0x8739F0 free pool slot, 0x873A30 free package slot, 0x873DF0 set package slot), and
  `sub_873D60` (free both pools), reached only from orphan code at 0x88E980.
- The PMCOM debug ring's case 23 call into `FFX_Ev_LoadSceneBackgroundAlt`, and the orphan third
  background loader at 0x86DD00.
- The premovie buffers `dword_1327110/14/18`, never allocated on PC.
- `qword_13270F8`. Use the package base.
- The `nullsub_33` and `nullsub_122` profiler markers, the alignment setter `sub_83C4A0` (use an
  aligned allocator), and the heap checks inside `tklib__f88D520`.
- The `off_C52768[11]` write for language 11, unreachable on PC.

---

## 7. Lockstep

Each finding is marked [V] or [I]. "Step thread" is the main thread that runs `FFX_MainStep`.

| # | what | kind | verdict | mark |
|---|---|---|---|---|
| L1 | **The title's load runs on a JobWorker thread.** `FFX_MainInit` (posted to a JobWorker at boot) calls `maybe_FFX_Scene_Init(save, 23)` at 0x820A98, so `FFX_Ev_LoadEventPackage`, `maybe_FFX_Map_SetupChars`, the CHR pool and the title presentation stage start off the step thread, and so do `FFX_Atel_Init`'s resets. Every later load runs on the step thread. | thread | Safe in the original only because the main thread waits for the job ring to drain. A recreation runs it on the step thread before step 0, which `BOOT_SEQUENCE.md` 11 already recommends | [V] |
| L2 | **Blocking reads inside the step**: `FFX_Ev_ReadEventPackageImage` (slot 2 with 0), two `FFX_Ev_PumpReadsBlocking(0)`, the background map out read, the resident CHR read in `SetupChars` | disk timing | A wall clock stall, never a step count difference. The completions they dispatch are ROM read completions only, all of them, in slot order. Lockstep safe. Long enough to matter to a hang watchdog | [V] |
| L3 | **Language picks the message tables and the font.** `FFX_LoadLocalizedBin/Ftc` build the path from `FFX_GetLanguage`, and a missing .bin falls back to the Japanese table inside the package | language, per machine file presence | Two peers on different languages run the same scripts against different text. The step only reads text through the message window layer (layout, choice detection, window size), so whether this desyncs is that layer's question. Safest rule: peers agree on the language, or the port treats text strictly as presentation | [V paths, I effect] |
| L4 | `FFX_Ev_BuildTexListPath` and `FFX_Path_RemapEventObjHost0` read the language | language | Presentation only (texture list, font page root) | [V] |
| L5 | `FFX_Ev_SetLocalizedEventDirForMode` reads the PS2 text language | language | Dead in retail | [V] |
| L6 | The per event text modes and font scale | event id | Deterministic, presentation | [V] |
| L7 | **Raw heap addresses.** The package image, the pool and the .bin are `FFX_MemAlloc` blocks whose addresses depend on the allocation history of the machine. Save 84 hands such an address to the script, and the pool is not cleared, so any read before write of pool memory is a per machine value (`ATEL_CONTEXT.md` 11.10). The image is rounded up to 2048 and the tail past the file is uncleared heap, which the legacy `+0x14` slot reads (`ATEL_CONTEXT.md` 2) | per machine value | A recreation zeroes every block, never lets an address into script state that is hashed, and keeps the package bytes plus a zeroed tail | [V] |
| L8 | Boot package and mgrp sizes are measured from disk on first use and cached | disk | Deterministic given the same files. The 399 mgrp slot reads a pointer value as its size (1.3), a value that does not depend on the machine but is wrong | [V] |
| L9 | EvTex textures arrive through the AsyncLoadManager and the draw retries until they do | disk timing | Presentation only, the step side never reads the texture | [V] |
| L10 | `FFX_Ev_IsBootSceneEvent` read on another thread: `FFX_Input__poll` is also called from `FFX_Input__pollThreaded` on the input thread (T6), reading `g_ffxCurrentEventId` | thread | Benign. T6 only polls during Lulu's fury minigame, and the value is a word written on the step thread | [V callers, I benign] |
| L11 | The layer reads no wall clock, no RNG and no pad | | Nothing to replicate | [V for the 16 bodies and the helpers in section 9] |
| L12 | The PMCOM ring can trigger a synchronous background load from a remote debug host | dev | Never fed in retail | [I] |

The order and timing hazards of the map change itself (the countdown counts `StepOnce` calls, the
load hold is disk paced, the step scale jump after the hold, CHRREG is disk paced) belong to the
request and the gameplay gate, not to this layer. `MAP_TRANSITIONS.md` 6 has them.

---

## 8. Existing names and comments that are wrong

| where | says | should say |
|---|---|---|
| `FFX_Ev_PumpReadsBlocking 0x88D1C0` (also proposed by `MAP_TRANSITIONS.md` 9) | always blocks | forwards its mode to loader slot 2. 13 of 16 call sites pass 1 and poll. Propose `FFX_Asset_PumpReads` |
| `FFX_Ev_AllocActorPool 0x88E650` | actor pool allocator | the generic 2 KB rounded, 64 aligned tklib allocation, used for the battle kernel and Save 84 too. Propose `FFX_Tk_AllocSectorRounded` |
| `FFX_Ev_DestroyEventModels 0x8742B0` and its thunk (`MAP_TRANSITIONS.md` 2.2 row 1 says "event model records") | event models | the EvTex sprite slots of Core 282..298. Propose `FFX_EvTex_ResetAllSprites` |
| `FFX_Ev_ReleaseSaveE0Buffers 0x874130` (`MAP_TRANSITIONS.md` 5.1) | releases buffers by an unknown mode | save `+0xE0` is the premovie state, the buffers are never allocated on PC. Propose `FFX_Premovie_ResetOnMapUnload` |
| `FFX_EvScript_*` x9 | an "event script" layer | `ch` syscalls. Propose `FFX_AtelSys_Ch_012_EnGravity_resi`, `_023_SetGravMode_resi`, `_031_IsMotionActive_resi`, `_037_SetMotionSlot_resi`, `_040_SetRunThreshold_resf`, `_092_SetNextHokan_resi`, `_093_GetMotion_resi`, `_094_GetMoveSpeed_resf`, and `FFX_AtelCh_SetMotionCommon` for the helper. Optional, the old names are not wrong about the body |
| IDB comment on `FFX_EvScript_chSetMotion` | variant 2 is `maybe_FFX_Ch_PlayMotion` | variant 2 is `FFX_Ch_SetMotionFramesInclusive` |
| IDB comment on `FFX_EvScript_chSetNextHokan` | `FFX_Ch_SetW73C` | `FFX_Ch_SetNextHokan` (the callee was renamed since) |
| `__cfltcvt_init_90 0x873BA0` | CRT float init | zeroes the six event package and pool slots, propose `FFX_Ev_ResetPackageSlots` |
| `tklib__f88D520 0x88D520` | module placeholder | `FFX_MemFree` then `SgMemCheckHeap(0)` and `(1)`. Propose `FFX_Tk_Free` |
| IDB comment on `FFX_Ev_LoadEventPackage` | the CHRREG boot task spawns the CHRs | already corrected by `MAP_TRANSITIONS.md` 7, still in the IDB |
| IDB comment on `g_ffxCurrentEventId` | "Suggested name g_ffxCurrentEventId. Not renamed" | stale, it is renamed |
| `MAP_TRANSITIONS.md` 8, open question 4 | save `+0xE0`, `+0xE4`, `+0xE8` writers not traced | `+0xE0` premovie state (Core 432, 433, 437, 449, `sub_873E90`), `+0xE4` written by Core 451 (`sub_874030`), `+0xE8` by Core 452 and 551 (`sub_873DD0`) |
| `MAP_TRANSITIONS.md` 8, open question 3 | save `+0xA8/+0xAA` names not settled | `+0xA8` = current BGM `dword_1327130` (20 BGM syscalls write it through `sub_873D00`) with a per track byte, `+0xAA` = `word_C322A8 + 1`, and `word_C322A8` is the premovie number and mode written by `sub_643AA0` from Core 432's loader. `sub_648540` (a save load path, mode 3) feeds a stored value minus 1 back into `sub_643AA0`. So the checkpoint snapshots the BGM and the premovie and a load re-requests the premovie [V writers and reader, I the purpose] |

### 8.3 Note on `dword_1327130`

It is read by 25 sites, all BGM syscalls, the title return and the checkpoint block. That makes it
the current BGM id, not an event layer value, but it sits in the same source file and the
checkpoint copies it, so it is named here.

---

## 9. Proposed names for the `sub_` the layer calls

The IDB was not touched. The same list is in
`C:\Users\Derek\AppData\Local\Temp\claude\e--Git-FFXCoopProject\35f9d94c-078a-4164-a1d1-386d72fd2d1d\scratchpad\evlayer_names.py`
as `NAMES`, with the renames of section 8 as `RENAMES` and global names as `GLOBALS`.

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x874B70 | `sub_874B70` | `FFX_EvTex_InitSprites` | only caller Core 282 InitEvTexSprites, sets the buffer, end, image var, enqueues the TexList |
| 0x8748A0 | `sub_8748A0` | `FFX_EvTex_SetSprite` | only caller Core 283, fills slot 0..127 from a sprite var |
| 0x8747F0 | `sub_8747F0` | `FFX_EvTex_ClearSprite` | only caller Core 284, clears the active byte |
| 0x8749B0 | `sub_8749B0` | `FFX_EvTex_SetSpriteAlpha` | only caller Core 285, byte 3 of the colour |
| 0x874AD0 | `sub_874AD0` | `FFX_EvTex_SetSpriteLayer` | only caller Core 286, high byte of word 0 |
| 0x874B30 | `sub_874B30` | `FFX_EvTex_SetSpritePos` | only caller Core 287, x and y words |
| 0x874A80 | `sub_874A80` | `FFX_EvTex_SetSpriteScale` | only caller Core 288, scale * 256 |
| 0x8749F0 | `sub_8749F0` | `FFX_EvTex_SetSpriteColor` | only caller Core 297, colour bytes 0..2 |
| 0x874970 | `sub_874970` | `FFX_EvTex_SetSpriteVisible` | only caller Core 298, slot byte +3 |
| 0x8749E0 | `sub_8749E0` | `FFX_EvTex_SetDrawEnabled` | only caller Core 575, `dword_1327F4C`, gates both the step and the draw |
| 0x8747B0 | `sub_8747B0` | `FFX_EvTex_GetSlot` | returns `&word_1327148[14 * clamp(i)]` |
| 0x8743E0 | `sub_8743E0` | `FFX_EvTex_StepSprites` | called twice by `FFX_Atel_StepFieldAll`, walks 128 slots into `sub_874BF0` |
| 0x874BF0 | `sub_874BF0` | `FFX_EvTex_StepSpriteAnim` | timer += 256, frame advance, end of animation clears the active byte |
| 0x7111E0 | `sub_7111E0` | `FFX_EvTex_DrawSlot` | only caller `FFX_EvTex_DrawSprites`, ensures the models then draws, destroys the six groups on failure |
| 0x7147B0 | `sub_7147B0` | `FFX_EvTex_CreateSlotModels` | builds the `%d_%d_%d.dds.phyre` texture name, `FFX_Vfx_CreateModel` for six keys |
| 0x639310 | `sub_639310` | `FFX_Gfx_EnqueueEventTexList` | wraps `sub_663F00` (`FFX_Ev_EnqueueTexList` in `ASYNC_LOAD_REQUESTS.md`), called with the TexList path by Core 282 and 283 |
| 0x639330 | `sub_639330` | `FFX_Gfx_ReleaseEventTexList` | called with the TexList path by the slot reset when the list was loaded |
| 0x67AA50 | `sub_67AA50` | `FFX_Ev_GetBootMotionGroupPath` | `off_C334D4[id - 393]`, the `sceneN00.mgrp` paths, used by the ROM read for sub-index 3 |
| 0x67AA70 | `sub_67AA70` | `FFX_Ev_GetBootMotionGroupSize` | the mgrp twin of `FFX_Ev_GetBootPackageSize`, cache `dword_C334A0` (six slots, see 1.3) |
| 0x67AB30 | `sub_67AB30` | `FFX_Ev_IsBootSceneEvent2` | same body as `FFX_Ev_IsBootSceneEvent`, three callers on the mgrp path |
| 0x67A9F0 | `sub_67A9F0` | `FFX_Ev_MeasureHost0File` | opens a host0 path, returns the size rounded to 16, "failed to open EternalCalm data file" |
| 0x76C320 | `sub_76C320` | `FFX_RomDev_GetSizeForIndex` | installed as loader slot 5 at 0x76CFFF in DVD mode, called by `FFX_Asset_GetSizeForIndex` on PC, HDD override then boot sizes then the fid table |
| 0x83C4A0 | `sub_83C4A0` | `FFX_Mem_SetNextAllocAlign` | `dword_1301964 = a - 1`, the allocator ANDs the block start with it at 0x83BEB3 to pad, every caller passes 64 right before `FFX_MemAlloc` |
| 0x873E40 | `sub_873E40` | `FFX_Ev_SetEventDirMode` | `byte_132711F = a`, from Save 9, Atel_Init (255) and the debug screen |
| 0x873AC0 | `sub_873AC0` | `FFX_Ev_GetActiveFtc` | returns `dword_1327120`, read by `FFX_Font_UploadFieldSheets` |
| 0x873CF0 | `sub_873CF0` | `FFX_Ev_SetActiveFtc` | writes `dword_1327120`, Atel_Init passes 0 |
| 0x873D60 | `sub_873D60` | `FFX_Ev_FreeAllActorPools` | frees both pool slots 0x23CBD78..0x23CBD80, only reached from orphan code |
| 0x86ED70 | `sub_86ED70` | `j_FFX_Ev_FreeAllActorPools` | thunk |
| 0x8799B0 | `sub_8799B0` | `FFX_EvDev_LoadDebugBdb` | builds `debug/<name>.bdb` through `sub_8795B0`, reads it into `dword_1328598[a1]`, dev gated |
| 0x879A70 | `sub_879A70` | `FFX_EvDev_LoadDebugSources` | walks up to 7 entries of the bdb, builds `.src` paths, `sub_879B60` each, dev gated |
| 0x879E70 | `sub_879E70` | `FFX_EvDev_FreeDebugBuffers` | frees `dword_1328598`, `dword_1328698`, `dword_13285B8` entries |
| 0x873C00 | `sub_873C00` | `FFX_Premovie_ResetState` | save `+0xE0 = 0`, the three buffers 0, from Atel_Init |
| 0x873EB0 | `sub_873EB0` | `FFX_Premovie_Load` | only caller Core 432 LoadPremovie, `sub_643AA0`, state 1, `+0xD8`, `+0xDC` |
| 0x873DB0 | `sub_873DB0` | `FFX_Premovie_Cancel` | only caller Core 433, state 6, number -1 |
| 0x873C40 | `sub_873C40` | `FFX_Premovie_SetState4IfActive` | only caller Core 449 (dead row), state 4 when 2..5 |
| 0x873E90 | `sub_873E90` | `FFX_Premovie_SetState` | save `+0xE0 = a`, title return and debug screen |
| 0x8740C0 | `sub_8740C0` | `FFX_Premovie_FreeBuffers` | frees the three premovie buffers when `a`, debug screen only |
| 0x873B10 | `sub_873B10` | `FFX_Premovie_GetState` | returns save `+0xE0` |
| 0x873B20 | `sub_873B20` | `FFX_Premovie_GetNumber` | returns save `+0xD8` |
| 0x873B30 | `sub_873B30` | `FFX_Premovie_GetMode` | returns save `+0xDC` |
| 0x873B00 | `sub_873B00` | `FFX_Atel_GetCurrentBgm` | returns `dword_1327130`, read by every BGM stop and fade syscall and the checkpoint |
| 0x873D00 | `sub_873D00` | `FFX_Atel_SetCurrentBgm` | writes it, from the BGM play syscalls, Atel_Init (-1) |
| 0x873E60 | `sub_873E60` | `FFX_Map_RequestZonePlain` | mode 2 and the plain id, Core 393 RequestMapZone |
| 0x874000 | `sub_874000` | `FFX_Map_RequestZoneNotify` | mode 1 and the notify id, also the sub scene, MapFx 6 SetZone |
| 0x873CC0 | `sub_873CC0` | `FFX_Map_ApplyPendingZone` | applies both pending ids, from `FFX_Field_MapRegLoadTask` |
| 0x874030 | `sub_874030` | `FFX_SaveData_SetFieldBootFlagsE4` | save `+0xE4 = a`, Core 451 |
| 0x873B40 | `sub_873B40` | `FFX_SaveData_GetFieldBootFlagsE4` | returns save `+0xE4` |
| 0x873DD0 | `sub_873DD0` | `FFX_SaveData_SetWordE8` | save `+0xE8 = a`, Core 452 and 551 |
| 0x873A90 | `sub_873A90` | `FFX_SaveData_GetWordE8` | returns save `+0xE8` |

Not named, on purpose: `sub_879820`, `sub_879840`, `sub_879860` (dev preload table getters whose
fields I could not tell apart), `sub_873AA0` (ASSET_PATHS already proposes
`FFX_Ev_GetLocalizedDirMode`), `sub_8795B0` (ASSET_PATHS proposes `FFX_Ev_BuildLocalizedDevPath`),
`sub_663F00` (ASYNC_LOAD_REQUESTS proposes `FFX_Ev_EnqueueTexList`), and the orphan slot helpers,
which are not functions in the IDB.

---

## 10. Open

1. Whether a retail run ever asks for event 399's motion group through the fallback, which would
   allocate 11.8 MB for `scene700.mgrp` (1.3). A probe: break on `sub_67AA70` with 399 during the
   quit to desktop sequence.
2. Whether any shipped map lacks a localized .bin, which would leave `g_ffxEvBuiltinMsgTable`
   pointing into the previous, freed image (section 5). The checker in `ASSET_PATHS.md` 8 could
   answer it from the archive.
3. What `sub_642C00` does inside `FFX_RomDev_PumpBlocking` while it waits (a keep alive present
   or render pump). It decides how the stall of L2 looks on screen, not the step.
4. Whether `sub_8827B0` (the other reader of the event dir mode) is reachable outside dev menus.
