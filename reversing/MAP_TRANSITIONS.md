# Map transitions: request, teardown, rebuild, scratch slot, what survives

Written 2026-10-13 for Project Zanarkand item 1.7.11. Every address is an IDA VA in FFX.exe
(image base 0x400000). Subtract 0x400000 for the RVA a kit hook wants. `[V]` means read in
Hex-Rays or the disassembly this session, `[I]` means inferred from names, shape or call sites and
not read end to end. The IDB was read only. Every `sub_` name below is still unnamed there and
section 9 proposes names.

This extends rather than repeats `WORLD_STATE.md` sections 5 and 6 (the request primitive and the
save block), `CHEAT_WARP.md` section 2 (the warp API and the hang), `ASYNC_LOAD_REQUESTS.md`
(H1, H2 and the ROM pump), `BOOT_SEQUENCE.md` step 110 (the title as event 23) and
`MAIN_LOOP.md` section 2 (the shape of `FFX_MainStep`). Read those for the background, this doc is
the end to end sequence.

## 0. The short answer

```
step S-n  a script syscall (Core 17/171/267/268) or a debug path calls
          FFX_Map_WarpToWithSavedFade / FFX_Map_WarpTo
            -> FFX_Map_RequestChange 0x88EA60: save +0x00/+0x02/+0x0C/+0x0D, g_ffxMapChangePending = 1
            -> FFX_Map_SetTransitionFrames 0x88EB50: both delay counters = n (0, 30, or save +0x12)
          scripts KEEP RUNNING while the counters drain, the fade out plays over them
step S    FFX_MainStep -> sub-step loop -> FFX_Atel_StepOnce 0x88D3D0 sees the counters at zero
            sub_639130                       loading indicator on (presentation)
            maybe_FFX_Scene_Init 0x88E070    THE WHOLE TEARDOWN AND REBUILD, synchronous, see 2 and 3
            FFX_Atel_StepFieldAll 0x872BD0   the new map's first ATEL step, the init phase
          rest of the same sub-step: MesWin, magic overlay, cameras, rcBg, CHR motion, player camera
          ++g_ffxMainStepCounter
step S+1  FFX_MainStep_IsFadeBlocking is true (gfx+4501 map scene pending, or async loads busy
..S+k     under the load hold fade), so the gameplay block is skipped. No ATEL, no motion, no
          step counter. g_ffxGameClock60Hz still ticks. k depends on disk speed and on a render.
step S+k+1 first gameplay step on the new map. The load hold fade ends, and
          FFX_MainStep_PublishStepScale publishes a step scale inflated by the k held steps.
```

Five facts that are new in this pass and change how earlier docs read:

1. **The warp gate is re-armed by every map load.** `sub_86EE10 0x86EE10` initialises an ATEL
   context and does `mov word ptr [edx], 9880h` at `0x86EF59`, which sets bit 0x80 of ctx byte 0.
   `maybe_FFX_Map_SetupChars` calls it for context 0 on every map load. That is the setter
   `CHEAT_WARP.md` 2.2 and the IDB comment on `FFX_Map_WarpToWithSavedFade` could not find. Their
   scan looked for `or ..., 80h` and missed a word store. So the rule is "one script warp per map
   lifetime unless debug mode", not "nothing on the play path arms it". [V store, V call chain]
2. **The scratch slot is a battle round trip mechanism, not a map transition one.** It is written
   only by `FFX_Btl_BeginBattle` (mode 0) and by `FFX_AtelSys_Save_036_start`, read only by the
   CHRREG boot task, and it is freed with the CHR pool on every map change. Section 4. [V]
3. **At the title (event 23) the CHR pool does not exist.** `maybe_FFX_Scene_Init` calls
   `FFX_Ch_EventjumpDispose` AFTER `FFX_Ev_LoadEventPackage` on the 23 arm, so the pool the load
   just allocated is freed again and `g_ffxChrArray` is NULL, `g_ffxChrCount` 0 for the whole
   title. [V order]
4. **The title checkpoint latch skips one eligible map, not two.** The counter is incremented
   first and compared with 2, so the first eligible map after the title does not write the
   checkpoint and the second one does. The IDB comment on `maybe_FFX_Scene_Init` says "the next
   two map entries" and the memory note says "stale for two map loads". Both are off by one unless
   one of the two loads was an excluded id (section 5.3). [V disassembly 0x88E211..0x88E22D]
5. **The first gameplay step after a load sees a disk dependent motion scale.** Section 6.2. [V
   mechanism, I size of the visible effect]

---

## 1. Who requests a map change

### 1.1 The request globals

| global | VA | written by | meaning |
|---|---|---|---|
| `g_ffxMapChangePending` | 0x133084C | `FFX_Map_RequestChange`, `FFX_Map_RequestResumeFromCheckpoint` set 1. `maybe_FFX_Scene_Init` clears | a request is waiting. The only real serialisation check [V] |
| `g_ffxMapChangeDelayFrames` | 0x1330850 | `FFX_Map_SetTransitionFrames`, resume and Scene_Init zero it | decremented once per `FFX_Atel_StepOnce` call [V] |
| `g_ffxMapChangeDelayFlag` | 0x1330854 | same as above | one more `StepOnce` of wait after the frames reach 0, then zeroed [V] |
| `g_ffxSceneLoaded` | 0x1330858 | Scene_Init sets 1 at the end, sets 0 just before its unload. Resume sets 0 | "a scene is live, unload it first". From outside the step it is 0 only between `RequestResumeFromCheckpoint` and its consumption, so `CHEAT_WARP.md`'s "zero means a load is in flight" is only true for that boot window [V] |
| `g_ffxQuitPhase` | 0x1327108 | `RequestChange(399)` and resume (never, see below) set 1. `FFX_MainStep` moves 1 -> 2 after 304 copyright frames | **399 is quit to desktop**, not "leave the field". Phase 2 makes `FFX_MainStep_CheckQuit 0x8738D0` post `WM_CLOSE`. The copyright screen only draws while the current event is a boot scene (393..399), so from a field map phase 1 just sits [V] |
| save `+0x00` word | 0x112CA90 | RequestChange | the target map (event) id. Scene_Init reads it back as its argument [V] |
| save `+0x02` word | | RequestChange (old +0x00), or `+0xC2` on the negative id and resume paths | previous map [V] |
| save `+0x0C` byte | | RequestChange | entry point index into the package's 32 byte doorway table [V] |
| save `+0x0D` byte | | RequestChange (old +0x0C), or `+0xCE` | previous entry point [V] |
| save `+0xB8`, `+0xBA` | 0x112CB48 | read and `+0xB8` zeroed by the negative id path and by resume | checkpoint entry and map [V] |

**Two requests before one consumption corrupt the previous map.** `RequestChange` does
`+0x02 = +0x00` unconditionally, and `+0x00` already holds the first, never loaded, target. So the
"previous map" after a double request is a map the player never stood on, and the first target is
lost. [V from the body] A recreation should refuse a second request while one is pending, or keep
the pre-request previous pair.

### 1.2 Every requester

All found from the xrefs to `FFX_Map_RequestChange 0x88EA60` (7), to `FFX_Map_WarpTo 0x86FEC0` (7)
and to `FFX_Map_WarpToWithSavedFade 0x86FF40` (4). [V]

| requester | VA | path | notes |
|---|---|---|---|
| Script doors, save points, airship, New Game, Load Game | `FFX_AtelSys_Core_017_resi 0x8581C0`, `_171_resi 0x858330`, `_267_WarpNoSavedFade 0x858460`, `_268_resi 0x858610` | `WarpToWithSavedFade(pop map, pop entry, 1 or 0)` | the four bodies are byte for byte the same apart from the third argument. 17 and 171 use the saved fade (`+0x11` style, `+0x12` frames), 267 and 268 use 0 or 30 frames. The title's Load Game is `core:267(-1, 0)`, the negative checkpoint form [V] |
| Game over | `FFX_Map_ReturnToTitle 0x86DA10` | 20 resets, arms ctx 0 bit 0x80 itself, `WarpTo(23, 0)` | reached from `maybe_FFX_Field_QueueBootTasks 0x861160` when the battle result is 1 and `g_ffxFieldBootMode 0x1326B48 & 0x200` is clear [V] |
| Debug | `maybe_SG_DebugGui_MapJumpItemProc 0x854DF0`, `maybe_SG_DebugGui_MiscItemProc 0x854C60`, `FFX_AutoTest_JumpMap 0x908100` (arms with `FFX_Map_ArmWarpGate 0x8729C0` first), `FFX_Debug_ApplyViewerSave 0x8B55E0`, `maybe_FFX_Atel__debugScreen` (three direct `RequestChange` calls) | `WarpTo` or bare `RequestChange` | see `CHEAT_WARP.md` [V] |
| Boot | `FFX_MainInit 0x820860` | `RequestChange(23, 0)` then `maybe_FFX_Scene_Init(save, 23)` directly | the title is loaded synchronously, before the first step [V] |
| Boot resume | `tklib__f88D710 0x88D710` | `FFX_Map_RequestResumeFromCheckpoint 0x88DD60` | sets `g_ffxSceneLoaded = 0`, so the consumer skips the unload. Two orphan callers at 0x88E9E0 and 0x88EC30 [V] |
| Battle return | none | | **a normal battle return does not change map.** `FFX_Btl_MainStep` queues the field boot tasks (section 4), it never touches `g_ffxMapChangePending`. Only the defeat arm reaches `ReturnToTitle` [V] |

### 1.3 What the warp wrapper adds on top of the request

`FFX_Map_WarpToWithSavedFade 0x86FF40`, in order [V]:

1. Gate: `*(char*)g_ffxAtelCtx < 0` (ctx byte 0 bit 0x80) or debug mode, else return.
2. `FFX_Atel_UnbindPlayerChr 0x8719E0`.
3. `FFX_Map_RequestChange(map, entry)`.
4. `dword_1327070()` if set (NULL in retail).
5. `FFX_Btl_CancelPendingBattle 0x781530` (zeroes `g_ffxBattlePendingKind 0x112A8E2`).
6. ctx byte 0 `&= ~0x80` (the gate is consumed), ctx `+0x210 |= 2` (makes
   `FFX_Atel_StepFieldFrame` cancel encounters and `FFX_MainStep_CheckQuit` stand down).
7. `sub_877450(-0.06666667f)`, which is `flt_1328310`, the screen fade rate.
8. Fade and delay. With the saved fade: `SetTransitionFrames(save +0x12)` then a fade request of
   that many frames through `sub_82CDC0 0x82CDC0`, style from save `+0x11` (0 and 1 select two
   styles). Without: `SetTransitionFrames(save +0x02 == 265 ? 30 : 0)`.

**IDB trap that hides step 8.** The two thin wrappers step 8 calls are named `_exit_1 0x82CBC0` and
`__tell 0x82CBE0` in the IDB, and `_exit_1` carries the noreturn attribute. Hex-Rays therefore
truncates `FFX_Map_WarpToWithSavedFade` at that call. Both are
`sub_82CDC0(frames, 0, style)`, which writes the fade request block `dword_1300704 |= 0x10`,
`byte_1300748` (kind), `word_130074A..E` (frames). [V]

**The gate, settled.** `sub_86EE10` stores `0x9880` to the first word of the context it resets
(`0x86EF59`), so byte 0 is 0x80, the gate bit. Its callers are `maybe_FFX_Map_SetupChars 0x87557C`
(context 0, every field map load), `sub_8756D0 0x8756E3` (from `sub_798390`, battle side) and
`sub_8759B0 0x875A1E` (from `sub_8BE3A0`). `FFX_Atel_InstallContextCallbacks` then ORs bit 0x01 in.
So field context 0 starts every map with the gate armed, a door script's warp consumes it, and the
next map load arms it again. [V store and callers, I that no other path clears it mid map]

### 1.4 The delay, exactly

```c
// FFX_Atel_StepOnce 0x88D3D0, the consumer, top of the function
if (g_ffxMapChangePending) {
    if      (g_ffxMapChangeDelayFrames > 0) --g_ffxMapChangeDelayFrames;
    else if (g_ffxMapChangeDelayFlag   > 0) g_ffxMapChangeDelayFlag = 0;
    else {
        if (map != 23 && map != 348 && !(map == 268 && FFX_Story_GetProgress() == 425))
            sub_639130();                          // FFXApplication+928 = 1, loading indicator
        maybe_FFX_Scene_Init(save, save.word[0]);
    }
}
FFX_Atel_StepFieldAll();                           // runs unconditionally, see 3.4
```

With transition frames `n`, the load fires on the `(n + 2)`th `StepOnce` call after the request when
`n > 0`, and on the first call when `n == 0`. A request made by a script inside `StepFieldAll` of
step S-1 is therefore consumed in step S at the earliest. [V]

`StepOnce` is called once per sub-step from the `FFX_MainStep` sub-step loop (0x820AE0, after
`FFX_Btl_MainStep`), so the countdown counts sub-steps, not presented frames and not
`FFX_MainStep` calls, and it freezes whenever the gameplay block is skipped (menus, save UI,
`FFX_MainStep_IsFadeBlocking`). [V]

**Scripts keep running during the countdown.** `StepFieldAll` runs after the request block every
call, so the old map's scripts, encounter check and player control all step while the fade out
plays. Only ctx `+0x210` bit 2 (set by the warp) suppresses the encounter. [V]

---

## 2. Teardown, in exact order

Everything below runs synchronously inside `maybe_FFX_Scene_Init 0x88E070`, inside one
`FFX_Atel_StepOnce` call, inside one sub-step of `FFX_MainStep`, on the simulation thread. Each
line was read in Hex-Rays. [V unless marked]

### 2.1 `maybe_FFX_Scene_Init` before the unload

| # | call | globals touched |
|---|---|---|
| 1 | `sub_643490(0)` | gfx `+4503 = +4502`, `+4502 = 0` (the battle map flag pair that `FFX_MainStep_IsFadeBlocking` reads through `sub_641BD0`) |
| 2 | `FFX_Came_SetScreenOffsetAndBase(0, 0)` 0x7BC720 | active screen `+48`, `+52`, `flt_1136FB8`, `flt_1136FBC` |
| 3 | trophy 20 latch | reads save `+0xC7F` (`byte_112D70F`) == 8 or 9, writes `byte_C58E8A`, may call `FFX_Trophy_Unlock(20)` |
| 4 | boot scene latch | `byte_18663D0 = 1`, `dword_18663D4 = 0` the first time a 393..399 event loads |
| 5 | `g_ffxAssetLoader[2](0)` = `FFX_RomDev_Pump(0)` 0x76C490 -> `FFX_RomDev_PumpBlocking 0x76E660` | **drains every in flight ROM read** (the 64 slot table at `dword_1127CB0`) and runs their completion callbacks through `FFX_AsyncLoad_DispatchCompletions`. So callbacks the old map queued land here, before the old map is torn down. Slot 2 is `g_ffxRomDevPump 0x2310C48`, installed at 0x76D060 [V] |
| 6 | `sub_648630(map)` | `dword_CCB990 = map` (gfx side copy of the scene id) |
| 7 | | `maybe_g_ffxSceneId 0x1330844 = map` |
| 8 | `FFX_Came_Init 0x7BD5D0` | all 3 camera slots `g_ffxCameSlots 0x1137540` and 3 screens `g_ffxCameScreens 0x1137000` cleared and re-linked, `g_ffxCameActiveSlot`, `g_ffxCameActiveScreen 0x23114C0`, `g_ffxCameOverrideScrDpt`, `g_ffxCameOverrideFlag`, `dword_2311424`, direct look at cleared |
| 9 | `sub_8AB7B0` | for the 8 message windows: `sub_906BA0(i)` and `FFX_MesWin_InstallTextSubStateHandler(i)` |
| 10 | `sub_8ADBF0` | zeroes the text, attribute and state heads of the 8 message window objects `g_ffxMesWinObjects` and `dword_1865B40..58` |
| 11 | if `g_ffxSceneLoaded`: `sub_8ADBF0` again, **`maybe_FFX_Map_Unload`** (2.2), `g_ffxSceneLoaded = 0` | |
| 12 | `sub_9DACA0(-1)` | `MagicFilePreload__clearPreloadedPrx(-1, -1)` when `dword_193FD40` |
| 13 | `sub_645310` | `sub_69F000(dword_CCCA6C)`, prints "VIRTUOS: Clear all VFX". The PC VFX layer. `RequestChange(23)` also calls it at request time |
| 14 | `FFX_Fmv_AbortForSceneChange 0x76ECE0` | if `g_ffxFmvScriptBlocking`: `g_ffxFmvScriptRunning = 0`, `dword_C40E2C = 1`, `g_ffxFmvScriptBlocking = 0`, `sub_63DF30` |
| 15 | `sub_642CC0` | gfx `+64648 = 0`, `+64652..+64712` back to identity colour scale and zero offset, `sub_671DA0` |
| 16 | **`FFX_Ev_LoadEventPackage(map)`** | the rebuild, section 3 |

### 2.2 `maybe_FFX_Map_Unload 0x872D20`

The big block only runs when context 0 is active (`sub_86AFC0(0)`, ctx byte 0 bit 0x01).

| # | call | what it resets |
|---|---|---|
| 1 | `sub_8742B0` | 128 event model records at `word_1327148` (stride 28) reset, every dyn geo group keyed on them destroyed (`FFX_DynGeoModel_DestroyGroupByKey`, 6 keys each), the event TexList released when `dword_1327140`, then `dword_1327140 = dword_1327144 = 0`, `dword_1327F48 = -160`, `dword_23CBC5C = 0`, `dword_1327F4C = 1` |
| 2 | | `dword_1327138 = dword_132713C = 0` |
| 3 | `op_event_effect_free 0x7FCBF0` | `dword_11A0080` flag bits, `byte_11A006B = 0` |
| 4 | `sub_886DD0(1)`, `sub_887780`, `FFX_Voice_RequestRefresh` x2, `sub_887B60` x2 | sound: `byte_1330091 = 1`, `dword_132FF1C = 0`, FMOD command 10, voice loader request flags `dword_13301E4/E8/EC` [I for the sound meaning] |
| 5 | `FFX_Btl_CancelPendingBattle` | `g_ffxBattlePendingKind = 0` unless the debug auto execute toggle |
| 6 | `sub_860F50` | `dword_1326B94..BD4` (16 dwords) zeroed, `dword_1326B94` recomputed as their OR |
| 7 | `FFX_Atel_SetSceneIdOverride(-1)` | `g_ffxAtelSceneIdOverride 0xC5273C = -1` |
| 8 | `FFX_AlBhed_StagePrimerGrantMask(0, 0)` | `dword_1325B90`, `dword_1325B94` |
| 9 | `sub_86FC20(0,0,0)`, `sub_86CFF0` | `dword_1326B50 = dword_1326B54 = 0`, `g_ffxFieldBootMode 0x1326B48 = 0x10001` |
| 10 | `sub_870280(-1,-1,-1,-1)` | `dword_1326CF0..CFC = -1` |
| 11 | `sub_919D70(0)` | guide map off: `sub_91D560(ctx+4, 0)` clears bit 0 of `g_rcbgGuideFlags` when `g_ffxRcBgCtx 0x193BF80` (`RCBG.md`) |
| 12 | `sub_8762F0` | 8 x 2 records at `unk_1328156` / `byte_1328172` cleared |
| 13 | `sub_8715E0(0)`, `FFX_Atel_SetMesWinBufSel(0)` | ctx `+68 = 0`, `g_ffxAtelMesWinBufSel = 0` |
| 14 | `sub_886A60(138)`, `sub_886800(138)` | two sound calls with id 138 [I] |
| 15 | | `dword_1327120 = 0` (the localised ftc alias) |
| 16 | `FFX_SaveData_ClearMapNameOverride` | **save `+0xD6` = 0** |
| 17 | | **save `+0xE4` dword = 0, save `+0xE8` word = 0** |
| 18 | `sub_642390(0)` | presentation stage 0 off (`sub_67F830`) |
| 19 | `sub_8AB550` | `Val = 0`, `byte_25CC5A0` cleared |
| 20 | `sub_788B90` | if `byte_1133364`: "EFFECT SYNC ERROR" assert, `byte_1133364 = 6`, `sub_788C00` |
| 21 | `sub_86D4C0` | `dword_1326B64 = 0`, `dword_1326B5C = dword_1326B60 = -1` |
| 22 | `_cfltcvt_init_92` (misnamed) | `dword_132FFA0..FDC` (16) = -1 |
| 23 | `sub_875430` | **disposes every type 1 actor's weapon and armour child CHRs** (`actor+2804`, `+2808`) and its held CHR `actor+156` when `+156` was a child, through `FFX_Ch_DisposeIfLive` |
| 24 | `sub_877330`, `sub_877460(0)`, `sub_877450(0)` | 16 records at `dword_1328318`, `flt_132830C = 0`, **`flt_1328310 = 0` (the fade rate)** |
| 25 | `sub_886CB0`, `_cfltcvt_init_91` | `dword_132FF2C..54` sound group slots |
| 26 | `sub_8661D0(1)` | sound command, then `sub_866520(ctx0 package, 1, -1)` |
| 27 | `sub_86A1C0(0)` | `g_ffxEvSeSepBlock[0] = 0` |
| 28 | `sub_86ED40(0)` | `g_ffxAtelActorCachePtr 0x13270E4 = 0`, **ctx 0 byte 0 bit 0x01 cleared** (context inactive), ctx `+500 = 0` |

Then, unconditionally:

| # | call | what it frees |
|---|---|---|
| 29 | `sub_8661A0` | ctx 0 byte 1 bit 0x80 cleared |
| 30 | `FFX_Tk_Free(g_ffxAtelActorPool)` | **actor pool `0x23CBD78` freed and zeroed** |
| 31 | `FFX_Tk_Free(g_ffxEvPackageBase)` | **event package `0x23CBD70` freed and zeroed** |
| 32 | `sub_887B50(0)` | `dword_132FF1C = 0` |
| 33 | `sub_874130` | reads save `+0xE0` as a mode. For values other than 1, 2, 3 and 5 it frees `dword_1327110/14/18` and zeroes **save `+0xE0`** |
| 34 | `sub_909E60` -> `sub_909EA0` | **map teardown**: walkmesh off (`FFX_Map_ActivateWalkmesh(0)` twice), `sub_919A60`, rcBg main teardown, `g_ffxMapOutSection0 = 0`, `g_ffxMapOutSection10` released, the map out blob `dword_19138A4` freed, `sub_90CD90(&g_ffxMapWork)`, `dword_1934ED4/D8 = -1`, and when `dword_1934EC4 > 0`: `sub_640090` (gfx scene release) and `FFX_ChCache_FreeUnlockedEntries`, then `sub_6403B0` (gfx scene state reset, TextureAnimationManager cleared), `sub_6403C0`, `sub_645860`. Back in `sub_909E60`: `sub_90CEA0(&g_ffxMapWork)`, `dword_1934EDC/E0 = -1`, `FFX_Map_ActivateWalkmesh(0)` |
| 35 | `j_FFX_Ch_EventjumpDispose` -> `FFX_Ch_EventjumpDispose 0x83B420` | **the CHR pool**, 2.3 |

### 2.3 The CHR pool teardown

`FFX_Ch_EventjumpDispose 0x83B420` [V]:

1. `sub_83C520(0)`: `byte_13019B5 = 0`.
2. `FFX_Ch_DisposeAll 0x8268E0`: for every slot in `g_ffxChrArray 0x23C44E4` with `m_inUse`,
   `FFX_Ch_Dispose 0x8266F0`, then `FFX_MemFree(g_ffxChrArray)`, `g_ffxChrArray = NULL`,
   `g_ffxChrCount 0x23C44E0 = 0`. The free takes the upper half with it, so **every scratch slot
   dies here**.
3. `FFX_Ch_DataDisposeAll 0x8259C0`: every live CHRDATA in `g_ffxChrDataTable 0x23C8D00` (40 x 300
   bytes) through `FFX_Ch_DataFree`, which also drops the four motion sets and the cache lock.
4. `FFX_Mot_ReleaseAllNonSystemBundles 0x836A80`: every motion bundle in `g_ffxMotBundleTable`
   except kind 2 (the system mgrp).
5. `sub_831A30` -> `sub_831BA0`.

`FFX_Ch_Dispose` per CHR: orphans its children, `m_inUse = 0`, hides the instance, **clears
`g_ffxControlledChr 0x1300788` (through `FFX_Ch_SetPlayerChr(NULL)`) and `g_ffxTidusChr 0x12FBC60`
when either one is this CHR**, frees parts, mesh pointers, joints, channels, collision volumes,
releases both render instances and the cloned ClassCharacter. [V]

### 2.4 What is NOT torn down here

- **ATEL contexts 1 to 6.** Only context 0 is deactivated (`sub_86ED40(0)`). The menu context 6
  and the others keep their state. [V that only ctx 0 is touched, I that nothing else resets them]
- **The CHR cache's locked entries** (`g_ffxChrCacheSlots`). Only unlocked entries are freed, and
  only when a map out was live. The package's resident list is re-aged in 3.2. [V]
- **The AsyncLoadManager queues.** Nothing here waits for or cancels a LoadCluster or RegistTask
  request. That is safe in practice only because the previous map's scene load must have
  installed before the gameplay block, and so `StepOnce`, could run at all (6.1). [V gate, I that
  no in flight cluster can reach a freed scene]
- **Sound streams in general.** Only the specific groups above are released. BGM continuity across
  doors is decided by the new package's BGM call in 3.1. [I]

---

## 3. Rebuild, in exact order

### 3.1 `FFX_Ev_LoadEventPackage 0x872EF0`

| # | call | globals touched |
|---|---|---|
| 1 | | `g_ffxCurrentEventId 0x12FBC40 = map` |
| 2 | `FFX_Atel_MesWinResetAll 0x863B10` | both message window buffers, `g_ffxAtelMesWinBufSel` |
| 3 | 13 small resets | `byte_1326B80 = 0` (`sub_870900`), `dword_1327138/3C = 0`, `g_ffxBattlePendingKind` (cancel), `dword_1326B6C = 100` (`sub_870F60`), `dword_1326CF0..CFC = -1`, `dword_1325B90/94`, `dword_1326B94..BD4`, `dword_1326B90` (`sub_863AE0`), `g_ffxAtelMesWinBufSel`, ctx `+68`, `dword_1326B48/50/54`, `FileHandle`(misnamed global)`/dword_1326B84/8C` (`sub_86E840(-1,0,0)`), `g_ffxFieldBootMode = 0x10001`, `g_ffxAtelSceneIdOverride = -1`, `byte_1326B81 = 0` (`sub_863F90`), `dword_1326CE0..CEC` (`_cfltcvt_init_89`), `dword_1326B5C/60/64` |
| 4 | `FFX_Ev_ReadEventPackageImage(12, 18*map)` 0x88D2C0 | allocates `size` rounded to 2 KB, zeroes the first dword, reads through slot 0, then **slot 2(0), a blocking ROM pump** ("EVENT READ SYNC"). `qword_13270F8 = image` |
| 5 | | `g_ffxCurrentEventName 0x23CBC60` from the path, `g_ffxEvMesWinBlitzTextMode 0x1327100` (bltz, hiku2, ids 23, 291, 93), `dword_1327104` (ids 79, 93, 97, 107, 110, 136, 140, 259, 291, 329), `g_ffxMesWinFontScale 0xC5D784` (0.9 for bltz, else 1.0) |
| 6 | magic check | `if (image[0] != 'E' || image[1] != 'V') while (1);` the unrecoverable hang for a missing id (`CHEAT_WARP.md` 2.5) |
| 7 | | old `g_ffxEvPackageBase` freed (already NULL after 2.2), new image installed |
| 8 | `FFX_Atel_GetResFlags`, `FFX_Atel_GetEntryMapNo(atel, 0)` | the background map number from doorway record 0 |
| 9 | `sub_781C50(entryMapNo)` | **per map encounter state**: `g_ffxStrollDistAccum 0x112A9E8 = 0`, `g_ffxStrollDistStep 0x112A9EC = 3.0`, `sub_788BE0` (`byte_1133364`, `dword_1133360`, `dword_113336C` = 0), **`FFX_Field_ResetEncounterAccum(1)`** (`g_ffxBtlReturnTasks = 0`, `g_ffxEncDistRemain 0x112A9D8 = 0`, `g_ffxEncDistTotal 0x112A9DC = 0`, `g_ffxEncNoBattleProd 0x112A9E0 = 1.0`, `g_ffxEncProbScale 0x112A9E4 = 1.0`), `sub_7828A0(map, 0)` which rewrites every encounter zone record's weight byte high nibble from its low nibble and recomputes the record total (so script adjusted encounter weights reset per map) |
| 10 | `FFX_Atel_CalcActorPoolSize`, `sub_88E650` | **actor pool** reallocated into `g_ffxAtelActorPool` |
| 11 | `FFX_LoadLocalizedBin`, `FFX_LoadLocalizedFtc` | `dword_1327128` (bin), `dword_132712C` and `dword_1327120` (ftc), `g_ffxEvBuiltinMsgTable 0x23CBD60`, `sub_8AC130` writes `dword_1841DA4..DF4`, `sub_642300(0, ...)` |
| 12 | title front end | map 23: `sub_642300(7, 0, 0)`. Otherwise `sub_642390(7)` if `g_ffxTitleScreenActive 0x25D5EBC`, then it is cleared |
| 13 | **`maybe_FFX_Map_SetupChars`** 0x875510 | 3.2 |
| 14 | `sub_88D1C0(0)` | blocking ROM pump again |
| 15 | `FFX_Atel_LoadEventBgm`, `FFX_Atel_StartEventSe(atel, table, 1)` | BGM and SE for the event |
| 16 | `sub_886DD0(0)`, `_cfltcvt_init_92`, `sub_866210(atel)` | `byte_1330091 = 0`, `dword_132FFA0..` = -1 |
| 17 | `sub_86DD30` or `sub_86DCA0(entryMapNo)` (res flag bit 0) | `FFX_SaveData_SetSceneAndSub(entryMapNo, 0)` (save `+0x04/+0x06/+0x08/+0x0A`, clears `+0x2A`), then `sub_9085D0` -> **`FFX_Map_LoadMapOutSync(19, 2m, 19, 2m+1, 0)`** 0x9093B0 and `g_ffxMagicDrawFlags 0x18DED08 |= 0x2000` |
| 18 | | `g_ffxPendingZoneMode 0x132711C = 0`, `g_ffxPendingZoneNotify = g_ffxPendingZonePlain = -1` |
| 19 | `sub_866490` if entryMapNo > 0 | ctx 0 byte 1 bit 0x80 set, guide map layout preset 0, `sub_91D930(ctx+4, 0)` (`RCBG.md`) |
| 20 | `maybe_FFX_Atel_RegisterSoundGroups`, `sub_885FF0(sub_86AD20(atel))`, `_cfltcvt_init_93` | `dword_132FF54`, `dword_132FF2C..44` |
| 21 | `FFX_Atel_LoadResourceList(atel, 1)` | the package's resource list [I contents] |
| 22 | `sub_887B60`, `sub_865AC0(atel)`, `sub_887B60`, `sub_860FD0`, `sub_863620` | `dword_1326B70`, `dword_1326B74` |
| 23 | | `byte_1327134 = 127`, `byte_1327135 = 63` |
| 24 | `sub_919D70(1)` | guide map enable (bit 0 of `g_rcbgGuideFlags`) |
| 25 | `sub_88D1C0(0)` | blocking ROM pump, last statement |

`FFX_Map_LoadMapOutSync 0x9093B0` reads the map out `.vpa` through slot 0 plus **a blocking slot
2**, stores `g_ffxMapOut 0x19138A8`, then for sections 0 and 1 calls `FFX_Gfx_FieldMapLoad 0x640210`
-> **`FFX_Gfx_BeginMapLoad 0x65CE90`**, which sets gfx `+4501 = 1` on the step and enqueues
`FFX_Gfx_MapLoadTask` on the AsyncLoadManager worker (`ASYNC_LOAD_REQUESTS.md` A6). That is the
only part of the map load that is asynchronous. It then activates the walkmesh for zone
`a5 + 2` (section 2 for zone 0), parses sections 10, 11 and 12, sets `g_ffxMagicDrawFlags |= 0x11`,
`dword_18DED0C`, and `sub_914250(&g_ffxMapWork)`. [V]

### 3.2 `maybe_FFX_Map_SetupChars 0x875510` and the CHR pool rebuild

1. `sub_86EE10(0, atel, pool, size)`: **context 0 reset**. Zeroes most of the 568 byte context at
   `g_ffxAtelCtxArray 0x1325BA0`, stores `0x9880` to its first word (**gate bit armed**), byte 2
   `= (old & 0x10) | 0xC8`, byte 3 = 0x30, actor counts from the ATEL header,
   `g_ffxAtelActorCachePtr = 0`. [V]
2. ctx `+52 = bin`, `+56 = ftc`, `+80 = 0`, `+76 = FFX_Atel_ContextStepCallback3x`,
   `FFX_Atel_InstallContextCallbacks(0)` (ctx byte 0 bit 0x01 set, the class 0 base
   `+44 = &save[+0x1EC]`), `FFX_Atel_SelectContext(0)`. [V]
3. For every actor: `sub_86CB00` (the actor constructor: zeroes the record, `+168 = 0`,
   `+170 = 0`, creates the two init threads on channels 7 and 0) and `sub_86D670`. [V]
4. Pool size: `ctx[+12] - ctx[+24] - ctx[+22] - ctx[+20] + FFX_Atel_GetSpareChrCount + 8`,
   floored at 36. [V]
5. **`FFX_Ch_EventjumpInit 0x83B460`**, in order: load hold fade (`FFX_ScreenFade_GetColor`,
   `SetColor(rgb, 1.0)`, `FFX_ScreenFade_SetLoadHoldMode(2)`, `FFX_ScreenFade_Begin`), floor the
   size at 10, `byte_13019B5 = 0`, `word_130073C`, `word_130072C`, draw switches back on
   (`g_ffxMapDrawEnabled`, `g_ffxChrDrawEnabled`, `g_ffxMesWinDrawEnabled`, `byte_12FB8FE`,
   **`g_ffxChrMotionUpdateEnabled 0x12FB8FF = 1`**, `g_ffxMapAlphaPassFirst 0x12FBBB6 = 1`,
   `g_ffxScriptEffectsPassOff = 0`), `g_ffxPlayerUseFixedYaw` off, `byte_13008C2 = 0`, water level
   and max to 1e6, four light presets through `sub_836520`, `maybe_g_ffxPlayerCtrlMode 0xC496D5`
   mode 2, `byte_13008BE..C1`, `dword_13008C4/C8`, `FFX_Map_ResetGroundTables` (`dword_1301A98..AE4`),
   player speed defaults, **`g_ffxMotKeepSpeed 0x12FBC34 = 1`**, then
   **`FFX_Ch_EventjumpRefreshCache 0x83B670`** (age the cache, move the package's resident CHR list
   to the back, free zero priority entries, re-lock the system mgrp, `FFX_Ch_RomRead` and
   `FFX_Ch_MotionSetReadStart` for every resident id, then **`FFX_Ch_RomPump(0)`, blocking**), and
   last **`FFX_Ch_AllocChrArray(size)`** 0x825670. [V]
6. `FFX_Ch_AllocChrArray`: `g_ffxChrCount = n`, `g_ffxChrNoFallbackOnce 0x12FBD98 = 0`,
   `g_ffxChrArray = FFX_MemAlloc(n * 0x1100)` (two CHRs per slot, the upper half is the scratch
   area of section 4), clears `m_inUse`, `m_name`, `m_instance` on the lower half only,
   `g_ffxChrRoundRobinCursor 0x13009E8 = 0`. The upper half is left as the allocator filled it. [V]
7. `FFX_Atel_StartEntryArrivalScript 0x86ED80`: creates the doorway arrival thread for save
   `+0x0C` on the map system actor. The thread does not run yet. [V]
8. `FFX_SaveData_SetMapNameIdFromGroundDic(0)`: **save `+0x0E`**. `sub_86F3C0(0)`: **save `+0x10` = 0**
   (the encounter ground attribute, rewritten every field step by `FFX_Atel_PullActorPosFromChr`).
   `sub_86CAE0` calls ctx `+80`, which step 2 set to 0, so it does nothing. [V]

**No CHR is spawned here.** The pool is empty after the rebuild. CHRs appear when the actors' init
scripts call `core:1` (`FFX_AtelOp_SetActorModel 0x85D000` -> `maybe_FFX_Ch_SpawnWithObjId`), which
happens in the first ATEL step, 3.4. The `CHRREG` boot task the IDB comment on
`FFX_Ev_LoadEventPackage` mentions is not queued on a map change at all. Its only queuer,
`maybe_FFX_Field_QueueBootTasks`, is called from the battle return and from
`FFX_AtelSys_Save_036_start`. [V xrefs]

### 3.3 `maybe_FFX_Scene_Init` after the load

| # | step | globals touched |
|---|---|---|
| 1 | | `dword_23CBEBC = 0` (propose `g_ffxCheckpointAutosavePending`) |
| 2 | map 23 | `g_ffxTitleExitCheckpointSuppress 0xC58E40 = 1`, `..Count 0x1330840 = 0`, **`FFX_Ch_EventjumpDispose` again (the pool built in 3.2 is freed, `g_ffxChrArray = NULL`)**, "Dispose all the chr cache elements.", `sub_643260(0)` (`dword_CCB468 = 0`) |
| 3 | ids 282, 194, 348, 349, 368, 255, 209, 212, 270, 320, 119, 247 and 393..399 | no checkpoint |
| 4 | title latch set | `++Count`, only when it reaches 2 is the latch cleared and the checkpoint written |
| 5 | checkpoint (`LABEL_28`) | `dword_23CBEBC = 1`, **save `+0xB8` = `+0x0C`, save `+0xBA` = `+0x00`**, **save `+0xA8` = `dword_1327130 \| byte_13300E0[that] << 8`**, **save `+0xAA` = `word_C322A8 + 1`**, `FFX_SaveData_SnapshotLocationHistory` (**save `+0xC0..+0xCE`**), `FFX_SaveFile_StageAndDescribe` (serialises the block into `g_ffxSaveFileStaging 0x1597F70`, `byte_1597F74 = 1`, platform save title strings), `sub_6484D0(0)` (copies the staged image to `dword_CE81F0`) |
| 6 | always | **`g_ffxMapChangePending = 0`, `g_ffxSceneLoaded = 1`, `g_ffxMapChangeDelayFrames = 0`, `g_ffxMapChangeDelayFlag = 0`** |
| 7 | | `dword_133086C = save +0x3D14` (`dword_11307A4`), read back by `sub_88D5C0` |
| 8 | `sub_67A5D0(map)`, `sub_67A6A0(map)` | FMOD commands 4 and 9 for the map through `FfxFmod_QueueCommand`, asynchronous [I that these are the map's sound banks] |
| 9 | `FFX_SyncData_RequestForMap(map)` 0x67A8E0 | the map's SyncData as a RegistTask (`ASYNC_LOAD_REQUESTS.md` A10, S2), asynchronous |

`dword_23CBEBC` is consumed by `FFX_Atel_PullActorPosFromChr 0x869E40` on the first field step
where the bound player actor has a CHR: it clears the flag and, when `sub_647E10(0)` reports
success, shows `chocobo.swf` (the autosave indicator). So **the PC autosave of a checkpoint is
written on the first bound step of the new map**, not inside Scene_Init. [V reads, I that
`sub_647E10` -> `sub_6F1C40` is the file write]

### 3.4 Which step is the new map's first simulation step

**Step S itself.** Scene_Init returns into `FFX_Atel_StepOnce`, which then calls
`FFX_Atel_StepFieldAll` unconditionally. Context 0 was just reset with byte 0 bit 0x08 clear, so
`FFX_Atel_ContextStepCallback3x` runs **`FFX_Atel_InitFrame(ctx, 1)`** rather than `StepFrame`: it
cancels any pending battle, runs every actor's script in mode 0 (the init entry), and sets bit 0x08
only when every actor reports its done bit. The CHR spawns (`core:1`) happen in this phase. Then
`FFX_Atel_StepFieldFrame` (the encounter check, a no-op this early), the menu open request and
`FFX_MainStep_CheckQuit`. [V call order, V InitFrame body]

The rest of the same sub-step then runs against the new world: `FFX_MesWin_StepAll`, the magic
overlay step, `FFX_Came_StepAll` on freshly initialised cameras, `FFX_RcBg_Step`,
`FFX_Ch_UpdateMotionAll(0.033373334)` over the CHRs the init phase just spawned, and
`FFX_PlayerCam_Step`. Then the display list half of `FFX_MainStep` and `++g_ffxMainStepCounter`.
[V order]

If the sub-step count is above 1, the remaining sub-steps of step S also run on the new map
(`StepOnce` again, so a second init frame or the first `StepFrame`). [V loop, I how often the count
is above 1 at a door]

From step S+1 the gameplay block is gated off until the map scene is installed (6.1). The init
phase may need more than one ATEL step, and those extra init frames only run once the gate opens.

---

## 4. The scratch slot

### 4.1 Where it is

`FFX_Ch_AllocChrArray` allocates `2 * count * 0x880`. For a CHR at index `i`, the scratch slot is
`g_ffxChrArray[i + g_ffxChrCount]`, the same index in the upper half.
`FFX_Ch_CopyToScratchSlot 0x828B30` is a single `qmemcpy` of 0x880 bytes into it and returns its
address. It is the only 0x220 dword copy in .text. [V]

### 4.2 Save: who, when, what

`FFX_Ch_CopyToScratchSlot` has one caller, **`sub_866240 0x866240`** (propose
`FFX_Field_FreezeActorsToScratch`), and that has two callers [V]:

| caller | VA | when |
|---|---|---|
| `FFX_Btl_BeginBattle` mode 0 | 0x78150B | a field encounter or scripted battle starting from the field. Before it sets battle sub phase 4. Hex-Rays hides this call because two callees above it carry a wrong noreturn flag, see the disassembly from 0x7814B0 |
| `FFX_AtelSys_Save_036_start` | 0x878C31 | the script command Save 36, which freezes, releases every actor CHR and re-queues the field boot tasks in place |

What `sub_866240` does, in order [V]:

1. `dword_132FF1C = 0`. Unless `sub_782A10()`, `g_ffxFieldBootMode 0x1326B48 = (B48 & ~0x10000) | save +0xE4`.
   `byte_1326BDD = !(B48 & 0x400)`. Pushes the `MUSIC` boot task. `word_1328274` cleared three
   times, sound slots `dword_132FF30..48` latched, `byte_1326B80 = 0`, `sub_909C60`
   (`dword_1931230`, `dword_1931254`).
2. For every context 0 actor (`word_1325BAC` of them): actor `+52 &= ~4`. If the actor has a CHR
   at `+156`: actor `+172 = FFX_Ch_GetB185(chr)`, actor `+176 = FFX_Ch_GetGroundMode(chr)`,
   **actor `+164` (`+0xA4`) = `FFX_Ch_CopyToScratchSlot(chr)`**, and actor `+51 |= 0x80` when actor
   `+56 & 8`.
3. For actors whose model id `+168` is 20481: up to four `sub_729570` effect handles at `+2840`.
4. ctx 0 byte 3 `|= 0x80`, player CHR unbound and conditionally rebound, ctx byte 0 bit 0x02
   restored, ctx 0 byte 1 bit 0x80 cleared.

The whole 0x880 byte CHR is saved, but only a subset is put back (4.3).

Then the field CHRs are destroyed: `maybe_FFX_Atel_ReleaseAllActorChrs 0x865DC0`, from
`FFX_Btl_SetupFieldPointers` on the battle path and directly from Save 36. Per type 1 actor it
records which motion sets were registered (`+54` bits 0x2000, 0x4000), saves the event hide state
to `+160`, disposes weapon and armour children and the actor's own CHR, and zeroes `+156`. The
array itself is not freed, so the upper half, and every scratch slot, survives the battle. Battle
CHRs live in the lower half. [V release, I that battle never allocates past `g_ffxChrCount`]

### 4.3 Restore: who, when, what

`FFX_Btl_MainStep` on battle end (`word_112A8E3` 5 or 6, or sub phase 33):
`FFX_Btl_CommitActorsToSave`, `FFX_Btl_Shutdown`, then
**`maybe_FFX_Field_QueueBootTasks(g_ffxBtlReturnTasks & 0xFF, packed, battleMap, result)`**. It writes the
result to **save `+0xD0`**, and unless the result is 6 (black fade, load hold 3) or a defeat that
returns to the title, pushes `CAMERA`, optionally `MOVIE`, `SNDCLR`, `SNDREG`, **`CHRREG`**,
optionally `EFFDONE`, `DONE` and `DONE2`. The queue runs from `FFX_Atel_ContextStepCallback3x`
(`FFX_BootTask_RunHead(0)`), one state per call. [V]

`FFX_Field_ChrRegTask 0x861850` is a per actor state machine. For each type 1 actor it reads the
model (`FFX_Ch_RomRead`, then **polls `FFX_Ch_DataReadSync` and returns to try again next step**),
weapon and armour, both motion sets, and in state 12 respawns the CHR
(`maybe_FFX_Ch_SpawnWithObjId(+168, +46)`), the children, then calls **`sub_862D70(actor, 1)`**
(propose `FFX_Atel_RestoreActorFromScratch`) unless actor `+54 & 0x40` or `+2832 & 1`. [V]

`sub_862D70` -> **`FFX_Atel_RestoreActorChrState 0x86E5B0`** -> **`FFX_Ch_CopyState(chr, actor+0xA4)`
0x828620**, which copies back position and previous position, `m_vec2C`, scale, offset, ground
normal, effect colour, rotation, move direction, ground height, run threshold, clip Z, the
`+0x180..+0x188` mode bytes, selected `m_flags1`/`m_flags2` bits, the look at block
`+0x40C..+0x4E7`, shade, motion ids, and re-resolves the parent CHR pointer from the parent actor
id. Then `sub_862D70` overrides position and facing from the ATEL actor's own motion state (actor
`+1368` or `+644`), with `Y - 0.1` except one hardcoded Tidus case, restores hide bits from
`g_ffxAtelPartyVisibleMask`, and re-applies a handful of per model, per map and per scenario
motion overrides (models 3, 7, 4222, 4223, 8409, 20481, maps by `FFX_Level_GetNameHash` 7382, 458,
4061). [V]

### 4.4 Lifetime

The scratch slot lives exactly as long as the CHR pool. It is allocated in 3.2, written at battle
entry or Save 36, read at the end of the following CHRREG, and freed in 2.3 on the next map change.
`actor+0xA4` points into the old pool after that, but the actor pool is freed at the same moment,
so nothing dangles. **It never carries anything across a map change.** [V]

Two consequences:

- A battle that starts and ends on the same map is the only round trip it supports. A battle that
  ends in a defeat takes the `ReturnToTitle` path and never restores.
- The CHRREG restore takes a **disk dependent number of steps**, because each `DataReadSync` that is
  not ready returns out of the task until the next ATEL step. The field comes back on different
  steps on two machines unless the reads are made step ordered (6.3).

---

## 5. What survives a map change

Classes: **survives** (not written by the transition), **reset** (forced to a constant),
**rebuilt** (freed and recreated from the new package or map files), **written** (set to a value
that depends on the transition). Each row was traced through the code in sections 2 and 3. [V
unless marked]

### 5.1 Save block `g_ffxSaveData 0x112CA90` (0x68C0 bytes)

| field | class | by |
|---|---|---|
| `+0x00` map, `+0x02` prev map, `+0x0C` entry, `+0x0D` prev entry | written | `FFX_Map_RequestChange` |
| `+0x04` scene, `+0x06` prev scene, `+0x08` sub, `+0x0A` prev sub, `+0x2A` from file flag | written, flag reset | `FFX_SaveData_SetSceneAndSub` from the entry map number |
| `+0x0E` place name id | written | `FFX_SaveData_SetMapNameIdFromGroundDic(0)`, then every field step |
| `+0x10` encounter ground attribute | reset 0, then written every step | `sub_86F3C0(0)`, `FFX_Atel_PullActorPosFromChr` |
| `+0xA8`, `+0xAA` | written on a checkpointing map | Scene_Init `LABEL_28` |
| `+0xB8`, `+0xBA` checkpoint | written on a checkpointing map, `+0xB8` zeroed when consumed | Scene_Init, RequestChange(-1), resume |
| `+0xC0..+0xCE` location history | written on a checkpointing map | `FFX_SaveData_SnapshotLocationHistory` |
| `+0xD0` | written by the battle return only | `maybe_FFX_Field_QueueBootTasks` |
| `+0xD6` map name override | reset 0 | `maybe_FFX_Map_Unload` |
| `+0xE0` | reset 0 (for modes other than 1, 2, 3, 5) | `sub_874130` |
| `+0xE4` dword (field boot flags ORed into `g_ffxFieldBootMode` at battle entry) | reset 0 | `maybe_FFX_Map_Unload` |
| `+0xE8` word | reset 0 | `maybe_FFX_Map_Unload` |
| `+0xBC` play time | survives, keeps ticking on the vsync thread during the whole load | not on this path (`THREADS_AND_CLOCKS.md`) |
| `+0x1EC..+0x21EC` ATEL class 0 script work (chests, doors, story) | **survives** | nothing on the path writes it. `WORLD_STATE.md` 5 |
| party, stats, items, gil, sphere grid `+0x21EC`, flags `+0x3DCC`, everything else | **survives** | |

### 5.2 Outside the save block

| global | VA | class | notes |
|---|---|---|---|
| `g_ffxRandStreamState` (68 streams) | 0x1135EE0 | survives | the only seeder is `FFX_Rand_SeedAllStreams 0x798890`, called only from `FFX_InitNewSaveData`. No reseed on a map change [V xrefs]. Init scripts of the new map may draw from it on step S, deterministically |
| `g_ffxBattleRandState` | 0xC42200 | survives | same seeder |
| cosmetic RNGs (`srand` stream, `g_ffxEffectRandState 0xC0A000`) | | survives | seeded once by `FFX_Rand_SeedCosmeticStreams 0x7FF360` from `FFX_MainInit`. Effect code re-seeds the CRT stream elsewhere (`MAGIC_DLL.md`) |
| `g_ffxMainStepCounter` | 0x23CBBF0 | survives | does not advance during the held steps of 6.1 |
| `g_ffxGameClock60Hz` | 0x12FB7F0 | survives | **does** advance during the held steps |
| `g_ffxStepScale88`, `g_ffxStepScaleFloat` | 0x23CBBEC, 0x23CBBE8 | written on the first gameplay step | 6.2 |
| encounter accumulators, stroll distance, zone weights | 0x112A9D4..0x112A9EC | reset | 3.1 row 9 |
| `g_ffxBattlePendingKind` | 0x112A8E2 | reset | three separate cancels on the path |
| `g_ffxCurrentEventId`, `g_ffxCurrentEventName`, `maybe_g_ffxSceneId`, `dword_CCB990` | 0x12FBC40, 0x23CBC60, 0x1330844, 0xCCB990 | written | |
| `g_ffxEvPackageBase`, `g_ffxAtelActorPool`, localised bin/ftc (`dword_1327128`, `dword_132712C`, `dword_1327120`), `g_ffxEvBuiltinMsgTable` | 0x23CBD70, 0x23CBD78 | rebuilt | ATEL classes 2, 3, 4, 5, 6 all live here, so per map script state restarts from the package's initial values (`WORLD_STATE.md` 5) |
| ATEL context 0 (`g_ffxAtelCtxArray`, `g_ffxAtelCtx0EventPkg 0x1325BE8`) | 0x1325BA0 | rebuilt | gate bit re-armed |
| ATEL contexts 1 to 6 | 0x1325BA0 + n*568 | survive | [I], only ctx 0 is touched |
| `g_ffxChrArray`, `g_ffxChrCount`, scratch slots | 0x23C44E4, 0x23C44E0 | rebuilt (NULL at the title) | 2.3, 3.2 |
| `g_ffxChrDataTable` entries, non system motion bundles | 0x23C8D00, 0x13011E0 | rebuilt | |
| CHR cache (`g_ffxChrCacheSlots`) | | partly survives | locked entries survive, unlocked ones freed with the map out, the resident list re-aged and re-read |
| `g_ffxControlledChr`, `g_ffxTidusChr` | 0x1300788, 0x12FBC60 | reset NULL | by `FFX_Ch_Dispose`, rebound later by the new map's scripts |
| cameras, screens, override | 0x1137540, 0x1137000 | reset | `FFX_Came_Init` |
| message windows | `g_ffxMesWinObjects` | reset | |
| map out, walkmesh, map work, rcBg | 0x19138A8, 0x1876E00, 0x193BF80 | rebuilt | |
| gfx map scene | `g_ffxGfxCtx 0xCCB9D8` +4500, +4501, +172, +140 | rebuilt asynchronously | 6.1 |
| gfx colour tweak floats +64648..+64712 | | reset | `sub_642CC0` and `sub_6403B0` |
| VFX, event dyn geo, event effects | | reset | `sub_645310`, `sub_8742B0`, `op_event_effect_free` |
| magic PRX preloads | | reset | `sub_9DACA0(-1)` |
| draw switches, motion update, alpha order, water, lights, player control mode, fixed yaw, keep speed | 0x12FB8FF, 0x12FBBB6, 0x13009D8, 0xC496D5, 0x12FBC34 and the rest in 3.2 step 5 | reset | `FFX_Ch_EventjumpInit` |
| screen fade | fade manager +74656, +74660 | written | load hold begun in EventjumpInit, ended on the first gameplay step |
| fade rate `flt_1328310`, `flt_132830C` | | reset 0 | Map_Unload. The warp's `-0.0667` lives until here |
| `g_ffxFieldBootMode` | 0x1326B48 | reset 0x10001 | |
| FMV script state | `g_ffxFmvScriptRunning`, `g_ffxFmvScriptBlocking` | reset | only if a movie was blocking |
| `FFXApplication+928` | | written 1, cleared by the indicator after it fades | presentation |
| title latch | 0xC58E40, 0x1330840 | written | 5.3 |

### 5.3 The title checkpoint latch, exactly

Loading event 23 sets `g_ffxTitleExitCheckpointSuppress = 1` and the count to 0. On every later
load that is not 23, not one of the twelve excluded ids and not a boot scene, Scene_Init does
`++count`, and **only when the new value is 2** clears the latch and writes the checkpoint. So the
first eligible map after the title keeps the loaded save's `+0xB8/+0xBA` and history, and the
second eligible map refreshes them. Excluded ids do not advance the count. A joiner or a desync
check that reads the checkpoint must expect it to lag by one eligible map after a title. [V
0x88E211..0x88E22D]

---

## 6. Order and timing a lockstep peer or a recreation must reproduce

### 6.1 The load hold is decided by disk and render, not by the step

`FFX_MainStep_IsFadeBlocking 0x81FCD0` is

```c
(FFX_Gfx_IsMapSceneLoading() && !sub_641BD0())                       // gfx+4501 && !(gfx+4502 || +4503)
|| (sub_641BC0() && sub_642610() && !sub_641BD0())                   // pending CHR attachments on 16 listed scenes
|| (!FFX_AsyncLoad_IsIdle() && FFX_ScreenFade_IsActive())             // any async load under an active fade
```

After step S, gfx `+4501` is 1 (set on the step by `FFX_Gfx_BeginMapLoad`, also written on the
worker in `FFX_Gfx_LoadMapAssetHeader`, H2) and is cleared only by `FFX_Gfx_InstallPendingMapScene
0x658AE0` during a render, after the worker has set `+4500`. The load hold fade from
`FFX_Ch_EventjumpInit` is active, so the third clause also holds while the gfx clusters, the
SyncData RegistTask or anything else is in the AsyncLoadManager queue. [V]

While it holds, `FFX_MainStep` takes the non gameplay tail: pads update, `FFX_StepPacing` (which
still does `g_ffxGameClock60Hz += 2`), `FFX_Chr_ProcessPendingAttachments`,
`FFX_ChrCache_StepDelayRelease`, save UI, `FFX_ScreenFade_Begin`, `FFX_MainStep_CheckQuit`. No ATEL,
no battle step, no CHR motion, no camera step, no `++g_ffxMainStepCounter`. [V]

The number of held steps k is a function of disk speed, worker scheduling and when a present
happens. **Two peers will not agree on k.** For lockstep this is fine as long as the hold itself is
not on the lockstep clock: the gate should be "both peers report the scene installed", then both
resume on the same step number. That is the same rule `ASYNC_LOAD_REQUESTS.md` gives for every load
edge.

### 6.2 The step scale jump after the hold

`FFX_MainStep_PublishStepScale 0x8206C0` runs only inside the gameplay block and does

```c
g_ffxStepScale88 = (g_ffxGameClock60Hz - dword_12FBC30) << 7;   // 0x100 for one step
dword_12FBC30    = g_ffxGameClock60Hz;
if (FFX_Player__getSubStepCount() != 1) g_ffxStepScale88 = 0x100;
```

The clock ticks during the k held steps but the snapshot `dword_12FBC30` does not, so the first
gameplay step after a load publishes `0x100 * (k + 1)`. Its one simulation reader is
`FFX_Mot_AdvanceFrame`, which scales the frame advance by it for every CHR with `m_flags1 &
0x100000` while `g_ffxMotKeepSpeed` is set, and `FFX_Ch_EventjumpInit` has just set it to 1. So any
such motion jumps forward by k steps on that one step. **This is a disk dependent value on the
simulation path.** [V mechanism. I how many CHRs carry 0x100000 at that moment, it is the
"keep speed" bit and may be rare right after a load]

A lockstep port must either feed both peers the same k (the host decides when the hold ends and
broadcasts the clock delta), or, simpler for a recreation, not let the clock advance while the hold
is on. The second changes behaviour only for that one jump.

### 6.3 Other order and timing facts

- **The whole teardown and the synchronous part of the rebuild run inside one step.** Event package,
  resident CHR list, map out `.vpa`, and every ROM read the old map left in flight are all pumped to
  completion before `maybe_FFX_Scene_Init` returns. That is a wall clock stall, not a step count
  difference, so it is lockstep safe. It is long enough to matter for the hang watchdog
  (memory note on the boot archive scan). [V]
- **The new map's init phase runs on step S, before the gfx scene exists.** Its CHR spawns and any
  RNG it draws are deterministic given the same step. [V order]
- **The countdown counts `StepOnce` calls**, so the sub-step count of each step and the gameplay
  gate must agree across peers during the fade out. [V]
- **The battle return CHRREG is disk paced** (4.4). Under lockstep the CHRREG task's
  `DataReadSync == 1` returns have to become "wait for every peer", or the field restore lands on
  different steps. [V poll, I effect on co-op]
- **The autosave is written on the first bound step** after a checkpointing load (3.3), on the
  simulation thread. It does not feed the simulation, but it is file I/O on the step thread and a
  recreation should move it off. [I]
- **`FFXApplication+928`, the loading indicator,** is set before the load except for maps 23, 348
  and 268 at story 425, drawn by `sub_6E50A0` after 1.5 s of real time and cleared there.
  Presentation only. [V writes, I the purpose]
- **`sub_639130` is skipped on exactly those three cases**, so a recreation that wants the same look
  needs the same three exceptions.
- **A missing package hangs on the simulation thread** (`CHEAT_WARP.md` 2.5). Only 330 of 402 ids
  ship. A recreation must validate before `RequestChange`, not at load time.
- **A double request loses the first target and poisons the previous map** (1.1).
- **ATEL contexts other than 0 survive.** A menu context left open across a warp keeps stepping on
  the new map. [I]

### 6.4 The sequence for a recreation, condensed

```
RequestChange(map, entry)            validate id, refuse if pending, write save +0x00/02/0C/0D, pending = 1
countdown                            n+2 StepOnce calls (n > 0) or 1, old scripts keep stepping
StepOnce on step S:
  loading indicator (presentation)
  Scene_Init:
    gfx battle flag shift, screen offset 0, trophy 20, boot latch
    drain ROM reads and run their callbacks
    scene id copies, cameras reset, message windows reset
    if live: Map_Unload
      (ctx 0 active) event models, effects, sound groups, battle cancel, field boot mode,
                     save +0xD6/+0xE4/+0xE8, child CHRs, fade rate, ctx 0 inactive
      free actor pool, free package, save +0xE0, map out + walkmesh + rcBg + gfx scene,
      CHR pool + CHRDATA + motion bundles (controlled and Tidus pointers cleared)
    magic preloads, VFX, FMV abort, gfx colour tweaks
    LoadEventPackage:
      event id, message windows, small resets
      read package (blocking), name, text modes, magic check or hang
      encounter state reset, actor pool, localised text
      SetupChars: ctx 0 reset (gate armed), actors constructed with init threads,
                  EventjumpInit (load hold fade, switches, resident CHR reads blocking,
                  CHR pool = 2 * n), arrival thread, save +0x0E, +0x10
      BGM, SE, scene words, map out (blocking) + gfx scene (async), zones, sound groups,
      resources, guide map on, pump
    title: free CHR pool again, latch
    else checkpoint unless excluded or latched
    pending = 0, loaded = 1, counters 0, FMOD map commands, SyncData request
  StepFieldAll: ctx 0 InitFrame, CHRs spawn
rest of step S on the new world, ++step counter
hold until scene installed and async idle (gate on all peers)
first gameplay step: hold fade ends, step scale (decide k, or freeze the clock during the hold)
```

---

## 7. Corrections to existing docs and IDB comments

| where | says | should say |
|---|---|---|
| `CHEAT_WARP.md` 2.2, IDB comment on `FFX_Map_WarpToWithSavedFade 0x86FF40` | the gate bit has exactly two setters, the title's own warp is unexplained | a third setter, the word store `0x9880` in `sub_86EE10` at 0x86EF59, re-arms ctx 0 on every map load |
| `CHEAT_WARP.md` 2.4 | `g_ffxSceneLoaded == 0` means a load is in flight | it is 0 only between `RequestResumeFromCheckpoint` and its consumption, and inside Scene_Init. Use `g_ffxMapChangePending` |
| `CHEAT_WARP.md` 2.6, `WORLD_STATE.md` 6 | 399 means "leave the field" | 399 sets `g_ffxQuitPhase = 1`, the quit to desktop sequence (copyright screen then `WM_CLOSE`) |
| IDB comment on `maybe_FFX_Scene_Init`, memory note "the title is just event 23" | the checkpoint is skipped for the next two map entries | the first eligible entry is skipped, the second writes it |
| IDB comment on `FFX_Ev_LoadEventPackage` | the CHRREG boot task spawns the CHRs | on a map change the init scripts spawn them through `core:1`. CHRREG is the battle return and Save 36 path |
| IDB comment on `FFX_Ch_CopyToScratchSlot` | used for map save and restore (also `FFX_GAME_NOTES.md`) | used for the battle round trip and Save 36, freed on every map change |
| IDB comment on `maybe_FFX_Map_Unload` | ends with EventjumpDispose so every transition frees the pool | correct, and on the title the pool is freed a second time after the load |
| IDB names `_exit_1 0x82CBC0`, `__tell 0x82CBE0` | CRT functions, `_exit_1` noreturn | fade request wrappers, the noreturn flag truncates every caller in Hex-Rays |

---

## 8. Open questions

1. **What does a map change requested during a live battle do?** `FFX_Atel_StepOnce` is in the same
   sub-step loop as `FFX_Btl_MainStep` and its request block has no battle test. `CHEAT_WARP.md`
   infers the flag waits for the field. I did not find what stops the field contexts stepping in
   battle, if anything does.
2. **`sub_642610`'s 16 scenes** (280, 82, 81, 222, 316, 381, 93, 18, 349, 248, 153, 179, 80, 311,
   363, 132) hold the gameplay gate while CHR attachments are pending. Why those scenes is not
   known.
3. **Save `+0xA8`/`+0xAA`.** Written from `dword_1327130` (set by `sub_873D00`, likely the current
   BGM) with a per track byte from `byte_13300E0`, and `word_C322A8 + 1` (a gfx side id set by
   `sub_643AA0`). The names are not settled.
4. **Save `+0xE0`, `+0xE4`, `+0xE8`.** Their writers outside this path were not traced.
5. **FMOD commands 4 and 9** from `sub_67A5D0`/`sub_67A6A0` with the map id. Probably the map's
   sound banks. If a later step waits on them, that is another disk paced edge.
6. **How often the 0x100000 motion bit is set on the first gameplay step** after a load, which
   decides whether 6.2 is visible in practice. A runtime probe: log `g_ffxStepScale88` and the
   count of CHRs with the bit on the first gameplay step after ten warps.
7. **Whether any ATEL context other than 0 is reset** by the new package's scripts at init.

---

## 9. Proposed IDB names

One row each. The IDB was not touched.

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x86EE10 | `sub_86EE10` | `FFX_Atel_ResetContext` | zeroes a 568 byte context, stores 0x9880 to its first word (arms the warp gate), loads the actor counts. Called per map for ctx 0 |
| 0x866240 | `sub_866240` | `FFX_Field_FreezeActorsToScratch` | the only caller of `FFX_Ch_CopyToScratchSlot`, stores the slot at actor+0xA4, called from battle entry and Save 36 |
| 0x862D70 | `sub_862D70` | `FFX_Atel_RestoreActorFromScratch` | the only caller of `FFX_Atel_RestoreActorChrState`, from CHRREG state 12 |
| 0x639130 | `sub_639130` | `FFX_App_ShowLoadingIndicator` | `FFXApplication+928 = 1`, read by `FFX_Boot_PresentFrame`, cleared by `sub_6E50A0` |
| 0x6E50A0 | `sub_6E50A0` | `FFX_LoadingIndicator_Step` | 1.5 s real time delay, fades, clears app+928 |
| 0x641BD0 | `sub_641BD0` | `FFX_Gfx_IsBattleMapFlagSet` | gfx +4502 or +4503, used twice by `FFX_MainStep_IsFadeBlocking` |
| 0x641BC0 | `sub_641BC0` | `FFX_Chr_GetPendingAttachCount` | returns `g_ffxPendingChrAttachCount & 0x7FFFFFFF` |
| 0x642610 | `sub_642610` | `FFX_Scene_IsAttachHoldScene` | true for 16 hardcoded scene ids |
| 0x648630 | `sub_648630` | `FFX_Gfx_SetSceneIdCopy` | `dword_CCB990 = a1`, called by Scene_Init |
| 0x8AB7B0 | `sub_8AB7B0` | `FFX_MesWin_ResetAllWindows` | 8 windows, reinstalls the text sub state handler |
| 0x8ADBF0 | `sub_8ADBF0` | `FFX_MesWin_ClearAllObjectText` | zeroes the heads of the 8 window objects |
| 0x9DACA0 | `sub_9DACA0` | `FFX_Magic_ClearPreloadedPrx` | `MagicFilePreload__clearPreloadedPrx(a1, -1)` when loaded |
| 0x645310 | `sub_645310` | `FFX_Vfx_ClearAll` | `sub_69F000`, prints "VIRTUOS: Clear all VFX" |
| 0x69F000 | `sub_69F000` | `FFX_VfxMgr_ClearAll` | same string |
| 0x642CC0 | `sub_642CC0` | `FFX_Gfx_ResetColorTweaks` | gfx +64648..+64712 to identity |
| 0x8742B0 | `sub_8742B0` | `FFX_EvTex_ResetAllSprites` | 128 records at `word_1327148`, destroys their dyn geo groups and the event TexList |
| 0x875430 | `sub_875430` | `FFX_Atel_DisposeActorChildChrs` | disposes `+2804/+2808` and child `+156` for type 1 actors |
| 0x909E60 | `sub_909E60` | `FFX_Map_UnloadField` | `sub_909EA0`, map work, walkmesh off |
| 0x909EA0 | `sub_909EA0` | `FFX_Map_TeardownMapOutAndScene` | walkmesh, rcBg, map out blob, gfx scene, CHR cache unlocked entries |
| 0x6403B0 | `sub_6403B0` | `FFX_Gfx_ResetMapSceneState` | scene lists, TextureAnimationManager__clearMap |
| 0x874130 | `sub_874130` | `FFX_Premovie_ResetOnMapUnload` | frees `dword_1327110/14/18` and zeroes save +0xE0 by mode |
| 0x86ED40 | `sub_86ED40` | `FFX_Atel_DeactivateContext` | clears ctx byte 0 bit 0x01 and +500, actor cache pointer |
| 0x86AFC0 | `sub_86AFC0` | `FFX_Atel_IsContextActive` | ctx byte 0 bit 0x01 |
| 0x86AFA0 | `sub_86AFA0` | `FFX_Atel_GetContextByIndex` | `&g_ffxAtelCtxArray[568 * a1]` |
| 0x86CAE0 | `sub_86CAE0` | `FFX_Atel_CallContextBootHook` | calls ctx +80 if set |
| 0x781C50 | `sub_781C50` | `FFX_Field_ResetPerMapEncounterState` | stroll distance, effect sync, encounter accumulators, zone weights |
| 0x7828A0 | `sub_7828A0` | `FFX_Btl_ResetEncounterZoneWeights` | rewrites each zone record's weight nibbles and total |
| 0x86DD30 | `sub_86DD30` | `FFX_Ev_LoadSceneBackground` | SetSceneAndSub then the map out load |
| 0x86DCA0 | `sub_86DCA0` | `FFX_Ev_LoadSceneBackgroundAlt` | same body, the other res flag arm |
| 0x9085D0 | `sub_9085D0` | `FFX_Map_LoadFieldMapOutSync` | `FFX_Map_LoadMapOutSync(19, 2m, 19, 2m+1, a2)` |
| 0x88D1C0 | `sub_88D1C0` | `FFX_Asset_PumpReads` | asset loader slot 2 |
| 0x88E650 | `sub_88E650` | `FFX_Tk_AllocSectorRounded` | 2 KB rounded `FFX_MemAlloc` for the actor pool |
| 0x86F3C0 | `sub_86F3C0` | `FFX_SaveData_SetEncounterGroundAttr` | save byte +0x10 |
| 0x86CFF0 | `sub_86CFF0` | `FFX_Field_ResetBootMode` | `dword_1326B48 = 0x10001` |
| 0x1326B48 | `dword_1326B48` | `g_ffxFieldBootMode` | low byte selects the battle return boot (1, 2 MOVIE, 3/4), bit 0x200 allows a defeat without the title, bit 0x400, 0x4000 skip CAMERA |
| 0x82CDC0 | `sub_82CDC0` | `FFX_ScreenFade_Request` | writes `byte_1300748` kind and `word_130074A..E` frames |
| 0x82CBC0 | `_exit_1` | `FFX_ScreenFade_RequestMode3` | `sub_82CDC0(a1, 0, 0)`. Also clear the noreturn flag |
| 0x82CBE0 | `__tell` | `FFX_ScreenFade_RequestMode5` | `sub_82CDC0(a1, 0, 1)` |
| 0x23CBEBC | `dword_23CBEBC` | `g_ffxCheckpointAutosavePending` | set by the checkpoint block, consumed on the first bound step to start the autosave |
| 0x133086C | `dword_133086C` | `g_ffxSave3D14AtMapLoad` | snapshot of save +0x3D14 at every map load, read by `sub_88D5C0` |
| 0x88D5C0 | `sub_88D5C0` | `FFX_Map_GetSave3D14AtLoad` | returns the above |
