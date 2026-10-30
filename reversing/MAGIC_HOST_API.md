# The magic host API table, slot by slot

PROGRESS 1.10.2 in projectzanarkand. `g_ffxMagicHostApiTable` at VA `0xC64CE8` (RVA `0x864CE8`)
is the table `MagicFile__start` hands to every `magic_NNNN.dll` through its `InitMagicPRX` export.
`MAGIC_DLL.md` covers the ABI, the loader and how DLLs reach the table. This doc names every slot.
Slot `i` is the dword at `0xC64CE8 + 4*i`.

Tags: [V] read in the binary at the address given, [M] measured by a scan, [I] inferred. A name
that starts `maybe_` is an [I] guess and says so. Every row named here has a repeatable comment that
starts "Host API slot N (usage)".

## Summary

1. **947 slots: 913 function pointers into `.text`, 33 data pointers, and 1 null at slot 854.**
   The file dword at `0xC65A40` is 0 [V]. `MAGIC_DLL.md` used to say 34 globals, the 34th was the
   null.
2. **Before this pass 453 slots had a placeholder name** (`sub_`, `nullsub_`, `unk_`, `dword_` and
   the like) and 36 more had a `maybe_` guess. The PROGRESS line says 772 because it was written
   before the battle, ATEL and ppp passes named the rest. **After it, 0 placeholders are left.** 895
   slots have a firm name, 51 a `maybe_` guess, and slot 854 is the null.
3. **489 rows are named or renamed here**: 432 placeholder functions, 21 placeholder globals, 35
   renames of earlier names (mostly `maybe_` guesses confirmed or corrected, plus one FLIRT
   collision), and the null row. 439 are [V], 49 [I], 1 [M].
4. **59 slots are never reached by any DLL**: 54 that no DLL touches and 5 reached only through a
   forwarding thunk nothing calls. 47 of them are named or renamed here (43 placeholders, 3
   `maybe_FFX_PrxPtr_*` guesses and the `__fileno` collision) and carry a comment that starts "No
   magic DLL calls this slot." or "Only an unreferenced DLL thunk reaches this slot." 13 of those
   bodies were not worth a real name and are called `FFX_HostApi_UnusedSlot<N>`. The other 12
   already had a name from an earlier pass and are listed in section 5.
5. **12 earlier names were wrong** and are corrected by the script. Two more name pairs, one IDB
   comment and two prototypes are wrong outside the slots and are only flagged. Section 6.
6. The apply script is `tools/apply_host_api_names.py`. It was rehearsed on a copy of the IDB:
   488 renames planned, 488 applied with comments and 0 failed, then a second dry run in the same
   session planned 0. The live IDB is untouched.

## 1. Method

The DLL side is measured, not guessed. `magicdll.HostCallScan` over all 581 DLLs (in memory, the
install is read only) gives each slot one of five reach kinds:

| reach | meaning | slots |
| --- | --- | --- |
| call | some DLL has a `call` through the table to it | 608 |
| thunk | reached only through the 17 byte forwarding thunks every DLL carries (`push ebp, mov ebp,esp, mov eax,[tbl], mov eax,[eax+d], pop ebp, jmp eax`), and some thunk is referenced | 247 |
| dead thunk | only an unreferenced thunk | 5 |
| copy | a data slot that `InitMagicPRX` copies into a DLL global | 32 |
| none | nothing | 55 (54 plus the null) |

The DLL count column in the tables is "DLLs that call it / DLLs that reach it through a referenced
thunk". A thunk reach says the slot is wired in, not that an effect calls it at run time.

The exe side was read in a scratch copy of the IDB, slot by slot: the full decompile, callers,
callees, strings and globals of each target, plus a disassembly of DLL call sites where the
arguments mattered. Names follow the layout law where it holds (functions sit by source file, so a
placeholder between two named functions of one library usually belongs to it), the strings the
body uses (shader parameter names such as `clrScaleFactor`, `Ball_Center`, `MaterialColour`,
texture names, error strings), and what the callers do with the result. One line forwarders get
the mechanical name of what they forward to.

The work was split into four address slices and the data slots. Slices A and D were named twice,
once by me and once by a worker on its own IDB copy, and where the two disagreed on meaning I
spot checked the worker's evidence in the binary and took the better grounded name. Slices B and C
are the workers' names, checked for collisions, duplicates and the comment rules.

## 2. By subsystem

| subsystem | rows | [V] or [M] | [I] |
| --- | --- | --- | --- |
| Yanagi effects (the 0x92D690.. task engine the magic scripts drive) | 79 | 75 | 4 |
| battle units | 76 | 68 | 8 |
| Oef2 interpreter | 26 | 10 | 16 |
| stubs and never-called forwarders | 24 | 14 | 10 |
| vector and matrix math | 22 | 22 | 0 |
| dyn geo (the VfxModel and VfxPart records) | 22 | 22 | 0 |
| app (Phyre application fog floats and fog setters) | 21 | 21 | 0 |
| vfx model draw (ppp MDL and shape draws, build job producers) | 21 | 19 | 2 |
| effect objects and prims | 21 | 20 | 1 |
| chr render instance (shader params, bounds, magic shaders) | 18 | 15 | 3 |
| effect units and registry | 18 | 17 | 1 |
| character access | 18 | 18 | 0 |
| gfx misc | 13 | 12 | 1 |
| sound | 12 | 12 | 0 |
| texture and GS | 12 | 11 | 1 |
| magic draw and map | 9 | 9 | 0 |
| lights | 8 | 8 | 0 |
| ppp operators | 7 | 7 | 0 |
| ppp core | 7 | 7 | 0 |
| camera | 7 | 7 | 0 |
| ppp runtime | 6 | 6 | 0 |
| camera and screen | 6 | 6 | 0 |
| file and loader | 6 | 5 | 1 |
| magic draw | 5 | 5 | 0 |
| PRX pointer | 5 | 5 | 0 |
| blur effect (BlurEffectEx) | 4 | 4 | 0 |
| specular chr | 4 | 4 | 0 |
| ATEL helpers | 4 | 4 | 0 |
| text and sprites | 4 | 4 | 0 |
| battle menu | 3 | 2 | 1 |
| CRT and memory | 1 | 1 | 0 |

The per subsystem tables with every row's evidence are in section 8, the full 947 row table in
section 9.

## 3. Things worth knowing that came out of it

- **`FFX_MagicApi_EffectRandFloat 0x9DBB40` (slot 241) re-seeds the CRT.** It mixes
  `g_ffxEffectRandState`, calls `srand(state & 0x7FFFFF)`, steps the state and returns a float. Any
  CRT `rand()` caller after it in the same step sees a sequence the effect chose. Three DLLs call
  it. `FFX_Vfx_DrawFrameBufferNoiseStrips 0x7116D0` (slot 860) and `FFX_Yanagi_RandDirXZ` (slot
  469) are CRT `rand` consumers on the DLL side. [V]
- **`FFX_Yanagi_GetUnitChr 0x92E1C0` (slot 481)** is the one place a DLL turns a unit id into a
  CHR: `FFX_Btl_GetUnitChr` in battle, `FFX_Atel_GetPartyCharChr` on the field. [V]
- **Slot 760 is `g_ffxYanagiCtx 0x25D5A20`**, the Yanagi effect context: +4 the effect handle
  `FFX_Btl_EffectSetReady` gets (`0x92DFBC`), +8 the Oef2 unit, +0x198 the caster, +0x19C the
  target count, +0x1A0 the target list. [V]
- **Fog has two colours.** `sub_908980` (slot 558) returns the application fog colour at +96, or
  the magic override colour at +80 while `g_ffxMagicDrawFlags` bit 2 is set. 116 DLLs write the
  override through `FFX_App_SetMagicFogColor` (slot 791). [V]
- **The ppp shader swap.** `FFX_Chr_ApplyMagicShader` (slot 855) clones a chr's materials onto one
  of three shaders (`CharacterBtmToTop.cgfx`, magic file 249, magic file 297) and
  `FFX_Chr_RestoreAllMagicShaders` (856) puts them back. [V]
- **`FFX_Vfx_GetModelRecord 0x6453D0` (slot 773, 232 DLLs)** is the dyn geo lookup by 64 bit key
  that most effect draws start from. [V]

## 4. The data slots

33 globals plus the null. 21 were placeholders and are named here, 12 had names already.

| slot | VA | name | tag |
| --- | --- | --- | --- |
| 741 | `0x230FFE0` | `g_ffxPppDefaultHeap` | [V] |
| 742 | `0x2332E8C` | `g_ffxEffectPacketCursorA` | [V] |
| 743 | `0x12A40C0` | `g_ffxEffectUnitPtrs` | earlier |
| 744 | `0x113FCD0` | `g_ffxEffectViewMatrix` | earlier |
| 745 | `0x113FD10` | `g_ffxEffectProjMatrix` | earlier |
| 746 | `0x2321540` | `g_ffxEffectRgSettings` | [V] |
| 747 | `0x119FE90` | `g_ffxBattleEffectUnit` | [V] |
| 748 | `0x2322780` | `g_ffxEffectDebugState` | [V] |
| 749 | `0x22FB450` | `g_ffxMagicDllSharedBlock` | [M] |
| 750 | `0x11A0060` | `g_ffxEffectSysFlags` | [V] |
| 751 | `0xC44BF0` | `g_ffxSinTable4096` | [V] |
| 752 | `0x230FF60` | `g_ffxVfxViewMatrix` | earlier |
| 753 | `0x230FD2C` | `g_ffxVfxPacketCursor` | earlier |
| 754 | `0x230FFA0` | `g_ffxPppPartWorldMatrix` | [V] |
| 755 | `0x230FE20` | `g_ffxPppDrawMatrix` | [V] |
| 756 | `0x230FE60` | `g_ffxVfxProjMatrix` | earlier |
| 757 | `0x230FF20` | `g_ffxPppModelViewMatrix` | [V] |
| 758 | `0x230FD34` | `g_ffxVfxTimeFrozen` | earlier |
| 759 | `0xC498DC` | `g_ffxSharedWorkBufPtr` | [V] |
| 760 | `0x25D5A20` | `g_ffxYanagiCtx` | [V] |
| 761 | `0x230FD30` | `g_ffxPppDrawHookTable` | [V] |
| 762 | `0x230FD50` | `g_ffxPppOwnerScale` | [V] |
| 763 | `0x11A0710` | `g_ffxGsDrawEnvPacket` | [V] |
| 764 | `0x23CBBF0` | `g_ffxMainStepCounter` | earlier |
| 765 | `0xC3E6C0` | `g_ffxClutSwizzleTable` | [V] |
| 769 | `0xC64CE0` | `maybe_g_ffxMagicHostApiVersion` | [I] |
| 827 | `0x113335C` | `g_ffxInMagicOverlayCall` | earlier |
| 850 | `0xCCCA58` | `g_ffxVfxDynGeoForceBlend4` | [V] |
| 854 | none | null pointer | [V] |
| 880 | `0x230FCA0` | `g_ffxVfxInvViewMatrix` | earlier |
| 919 | `0x1133398` | `g_ffxMagicSpecialActorList` | [V] |
| 921 | `0xC3BBB0` | `g_ffxPppOpTableForDlls` | earlier |
| 935 | `0x1940AC4` | `g_ffxMagicHideChrOnStop` | [V] |
| 936 | `0x12FB7D0` | `g_ffxIsCatchUpStep` | earlier |

The eleven slot cluster `MAGIC_DLL.md` pointed at, `0x230FCA0..0x230FFE0`, is the ppp and vfx
draw state: view, inverse view and projection matrices, the part world, draw and model view
matrices, the draw hook table, the owner scale, the packet cursor, the time freeze flag and the
default ppp heap. 743 shows as never reached in the tables because DLLs load it and index it rather
than copying it, which the copy scan does not count (`MAGIC_DLL.md` section 3).

## 5. Never-called slots that already had a name

These were named before this pass, so the script does not touch them and they carry no "No magic
DLL calls this slot." comment. Listed so the never-called set is complete.

| slot | target | name | note |
| --- | --- | --- | --- |
| 2 | `0x824650` | `FFX_Ch_MarkDirty` | |
| 392 | `0xA8D87A` | `abs` | CRT |
| 430 | `0x9304D0` | `j_maybe_FFX_YanagiTask_PopStateFlag2` | dead thunk, see section 6 |
| 452 | `0x949CF0` | `rand_0` | CRT |
| 725 | `0x94964C` | `memset` | CRT |
| 743 | `0x12A40C0` | `g_ffxEffectUnitPtrs` | reached by indexing, see section 4 |
| 770 | `0x643900` | `FFX_ScreenFade_SetColor` | |
| 838 | `0x63CDD0` | `FFX_ChrInstance_SetShadeCoeff` | |
| 839 | `0x63D050` | `FFX_ChrInstance_SetShadeColor` | |
| 840 | `0x645400` | `FFX_Host_ResolveTexByName` | |
| 844 | `0x830CD0` | `FFX_Ch_SetLayerByteAndBreak` | |
| 868 | `0x6438E0` | `FFX_ScreenFade_SetBlendMode` | |

The full table in section 9 marks every never-called slot, 59 in all, with ", never called".
Section 9 also shows `maybe_` names that were already guesses before this pass and are not
touched here as "guess, earlier".

## 6. Wrong names

Corrected by `apply_host_api_names.py` (RENAMES, each comment starts "was OLD."):

| VA | was | now | why |
| --- | --- | --- | --- |
| `0x86AE90` | `maybe_FFX_Atel_PartyCharHasPendingScript` | `FFX_Atel_GetPartyCharChr` | returns actor +0x9C, the CHR, when attach kind +0xAA is 1. Not a pending-script test. MapFx 088, Save 053 and `FFX_Yanagi_GetUnitChr` use the result as a CHR [V] |
| `0x63C430` | `maybe_FFX_ChrInstance_SetBoneMatrix` | `FFX_ChrInstance_SetMeshTransformById` | calls `FFX_Gfx_SetMeshTransform` for every mesh whose id (+24) matches, no bone involved [V] |
| `0x63FCC0` | `__fileno` | `FFX_VfxPart_GetWord16` | FLIRT collision. The body is `return *(a1+16)` among the VfxPart one line getters [V] |
| `0x795980` | `maybe_FFX_Btl_GetUnitCenterPosAlt` | `FFX_Btl_GetUnitTopPos` | y minus full height grounded, minus half airborne, with +Y down that is the top [V] |
| `0x7960D0` | `maybe_FFX_Btl_SetUnitFlag1056` | `FFX_Btl_SetUnitMoveArcMode` | BTLUNIT +0x420, the arc mode Btl 028 writes and `FFX_Btl_BuildArcPath` switches on [V] |
| `0x796110` | `maybe_FFX_Btl_SetUnitFlag1042` | `maybe_FFX_Btl_SetUnitMotionHold` | BTLUNIT +0x412, idle motion is only picked while it is 0. Still a guess [I] |
| `0x7E4E80` | `maybe_op_oef2_clut_load_pal` | `FFX_Tex_ReloadGroup` | loads no CLUT, re-runs `op_oef2_texture_load` for every group entry sharing a mask bit [V] |
| `0x7FDC80` | `maybe_FFX_Effect_StepAll` | `FFX_Effect_DebugReloadFromHost` | not a step, reloads effect unit data from host files under the debug flag [V] |
| `0x833D10` | `maybe_FFX_Ch_GetJointMatrix` | `FFX_Ch_GetJointWorldPoint` | outputs a point, the joint world matrix times an offset [V] |
| `0x906680` | `maybe_FFX_Magic_SetTexParam` | `FFX_Magic_SetSpriteVertexColor` | writes the colour of the sprite corner records, beside the pos and uv setters [V] |
| `0x9088D0` | `maybe_FFX_MagicDraw_GetVec4` | `FFX_MagicDraw_GetColorScale` | `flt_18DF00C` is the per channel colour scale the fog colour getter multiplies by [V] |
| `0x90A270` | `maybe_FFX_MagicDraw_SetVec4` | `FFX_MagicDraw_SetColorScale` | write side of the same scale [V] |

The other renames drop a `maybe_` the body confirms, for example `FFX_Mtx_RotZYX 0x93D550`,
`FFX_Vfx_FreePrimList 0x711F20`, `FFX_Vfx_ForEachPrim 0x716710` and `FFX_PrxPtr_Get 0xA44650`.

Flagged, not changed by the script (not host slots, or a comment rather than a name):

| VA | name | proposal |
| --- | --- | --- |
| `0x7FC650`, `0x7FC780` | `FFX_Ch_ReleaseShadowRegistryEntry`, `FFX_Ch_RegisterShadowRegistryEntry` | `...EffectRegistryEntry`. The registry at `0x12A22C0` links a CHR to its effect unit and mask slot, nothing about shadows. `CHR_POOL.md` already proposes this. The new slot names here (309, 310, 335, 336, 547, 548) use EffectRegistry |
| `0x92EE00`, `0x9304D0` | `maybe_FFX_YanagiTask_PopStateFlag2` and its `j_` thunk (slot 430) | `FFX_YanagiTask_PopStateAndRerun`. Bit 2 makes the runner `sub_92ED90` rerun the task in the same frame, the same reason slot 461 is `FFX_YanagiTask_SetStateAndRerun` |
| `0x8392C0` | `FFX_Mot_IsRequestActive` | name is right, its IDB comment says "finished" but `FFX_Btl_IsUnitMotionDone 0x787840` and Btl 008 WaitMotion treat non-zero as still playing |
| `0x82CB80`, `0x82CBC0` | `FFX_ScreenFade_RequestMode2`, `Mode3` | prototypes still carry `__noreturn`, which their own comment says breaks callers in Hex-Rays |

## 7. What is left

- **Slot 854** is a null in the exe. Nothing to name.
- **Slots 423 and 425**, `maybe_FFX_Gs_LoadImage423_Stub` and `maybe_FFX_Gs_LoadImage425_Stub`,
  return 0 and only DLL thunks reach them. Left as they are. Slot 424 between them, which DLLs call
  with the buffer slot 423 just uploaded, is `maybe_FFX_Gs_ImageSync_Stub`.
- **51 `maybe_` names** stay guesses. The least sure: the Oef2 `u2_part` handlers (728..740), the
  three empty effect hooks in slice C (`maybe_FFX_Effect_EmptyHook`, `PostFreeNop`,
  `PostInitUnitNop`), `maybe_FFX_Vfx_BuildEmulatedVuModelPacket 0x7ED0C0` (31 KB, one DLL),
  `maybe_FFX_Oef2_DllOp_DrawTexMeshOrBranch 0x7F5F70`, `maybe_FFX_Vfx_DrawFrameBufferMesh
  0x7117C0` and `maybe_FFX_Vfx_DrawMdlImmediate 0x711830`.
- Several names carry a field offset (`FFX_App_GetFloat208`, `FFX_VfxPart_GetWord16`,
  `FFX_ChrInstance_SetWord28`, `FFX_App_SetFloat220`) because the body is a one line accessor and
  no reader says what the field means. They are [V] for what they do.

## How to apply

```
"C:/Users/Derek/AppData/Roaming/Hex-Rays/IDA Pro/venv/Scripts/python.exe" -I tools/apply_host_api_names.py --idb <copy>
"C:/Users/Derek/AppData/Roaming/Hex-Rays/IDA Pro/venv/Scripts/python.exe" -I tools/apply_host_api_names.py --idb <copy> --apply --comments
```

The first is a dry run. The script never saves, and it refuses a row whose current name is not
what it expects or whose new name is taken, so a second run is a no-op.

## 8. Per subsystem tables

### Yanagi effects (79 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 515 | `0x9249A0` | `FFX_Yanagi_GetCasterFacing` | 3 / 0 | [V] | Returns flt_25D5EA4, which FFX_Yanagi_BuildTargetTable sets to the caster's wrapped battle rotation (sub_92DFD0) at 0x924A20. |
| 516 | `0x9249C0` | `FFX_Yanagi_GetTargetDepth` | 3 / 0 | [V] | Field +16 of target record a1 in the 36 byte table at word_25D5C60, the z of the target offset rotated into the caster's frame (0x924A20 stores v[2] there). |
| 517 | `0x9249E0` | `maybe_FFX_Yanagi_GetTargetAngle` | 3 / 0 | [I] | Field +12 of target record a1, the result of sub_92EC20 on the normalised caster to target direction. sub_92EC20 is presumably an atan2, not read. |
| 518 | `0x924A00` | `FFX_Yanagi_GetTargetDistance` | 3 / 0 | [V] | Field +8 of target record a1, the horizontal caster to target distance (length with y zeroed) written at 0x924A20. |
| 519 | `0x924A20` | `FFX_Yanagi_BuildTargetTable` | 3 / 0 | [V] | For up to 16 targets of the Yanagi context (count dword_25D5BBC, ids dword_25D5BC0) gets a bone position, rotates the offset from the caster bone into the caster frame and records id, distance, angle and depth (36 bytes each). Also averages the positions into unk_25D5EA8. |
| 526 | `0x925130` | `FFX_Yanagi_StepScreenFade` | 1 / 0 | [V] | sub_924C70(&g_ffxYanagiCtx), the screen fade step FFX_Yanagi_StepProgram also runs. It interpolates the fade colours and queues FFX_Gfx_QueueFadeQuad1000. |
| 435 | `0x9251C0` | `FFX_Yanagi_FreeProgram` | 40 / 0 | [V] | Makes program+16 the current ppp heap, frees the sub program array at +24 (FFX_Ppp_HeapFreeBlock) and every task in the list at +28 (sub_930260). magic_0187 calls it from overlay slot 1, the free slot. |
| 438 | `0x9254F0` | `FFX_Yanagi_SetPppHeap` (was `maybe_FFX_Yanagi_SetPppHeap`) | 40 / 0 | [V] | *g_ffxPppCurrentHeap = task +16, so ppp allocations go to the task's heap. Confirms the guess. Slot 439 is a byte identical twin. |
| 439 | `0x925510` | `FFX_Yanagi_SetPppHeapAlt` | 40 / 0 | [V] | Same body as FFX_Yanagi_SetPppHeap 0x9254F0, *g_ffxPppCurrentHeap = task +16. A second host entry point for the same operation. |
| 458 | `0x925590` | `FFX_Yanagi_StartPartA` | 14 / 0 | [V] | Gets sub table 4 entry a3 of the task's Oef2 unit and starts a part slot at a2 +816 with it (sub_92F470 marks the slot live). FFX_YanagiTask wait 0x930350 polls the same +816 slot. |
| 490 | `0x925690` | `FFX_Yanagi_StartPartB` | 3 / 0 | [V] | Sub table 4 entry a3 started in the part slot at a2 +960 through sub_92F6B0, which also wires off_C498DC and the ppp matrices at 0x230FD60 and 0x230FDA0 into it. |
| 440 | `0x9256D0` | `FFX_Yanagi_TaskSetParticle` | 38 / 0 | [V] | (program, task, subProgram, part): sub_92EEE0 points task slot +640 at entry part of sub program subProgram (program+24), with matrix +496, colour +352 and +368. Slot +640 runs FFX_Ppp_StartPart in sub_92F010. Also called by sub_9253A0. |
| 527 | `0x925710` | `FFX_Yanagi_SetScreenFadeManual` | 1 / 0 | [V] | dword_25D5C14 (program+500) = a1, and when nonzero also +448 = 3. With +500 set FFX_Yanagi_StepProgram skips sub_924C70 (0x9254B0 test), so the DLL steps the fade itself via host 526. DLL 0529. |
| 496 | `0x92D7E0` | `FFX_Yanagi_RadToPs2Rot` | 1 / 0 | [V] | Scales a rotation vec4 in place by -651.8986 (-4096/2pi) and subtracts 1024 from y, radians to PS2 angle units with the -90 degree yaw offset. Used before sub_7E9A30 in 0x92D8C0 and 0x92D9A0. |
| 497 | `0x92D810` | `FFX_Yanagi_WrapAngle2Pi` | 1 / 0 | [V] | Wraps an angle into 0..2pi with fmod (0x92D810). |
| 454 | `0x92D860` | `FFX_Yanagi_HideChrSelfEffects` | 1 / 0 | [V] | Pure forward to host 309 sub_7FBA40(chr), which sets bit 4 of the CHR's shadow registry flag byte. The same callee backs ATEL Core 463 HideSelfChrEffects. |
| 455 | `0x92D870` | `FFX_Yanagi_ShowChrSelfEffects` | 1 / 0 | [V] | Pure forward to host 310 sub_7FBAB0(chr), which clears bit 4 of the CHR's shadow registry flag byte. Same callee as ATEL Core 462 ShowSelfChrEffects. |
| 478 | `0x92D880` | `FFX_Yanagi_StartMotionBlur` | 3 / 0 | [V] | (mode, strength, frames): program +536/+540/+544, counter +548 = 0, active +552 = 1. Each step sub_924BF0 feeds host 543 sub_82BEE0 (the motion blur setter ATEL Sg 010 uses), fading the strength out in mode 2, and sends 0 when the frames run out. |
| 498 | `0x92D8C0` | `FFX_Yanagi_BuildUnitJointMatrix` | 1 / 0 | [V] | Builds a matrix from a PS2 rotation and position (sub_7E9A30), the unit's joint a2 matrix (sub_833B80) and a 0.001 scale (sub_92DE80) and multiplies them into a3. |
| 479 | `0x92D9A0` | `FFX_Yanagi_BuildUnitJointMatrixAtPos` | 2 / 0 | [V] | The sub_92D8C0 build plus the unit's battle position (FFX_Btl_GetUnitPos) and a translate back, so the result is relative to the unit. |
| 499 | `0x92DD30` | `maybe_FFX_Yanagi_TintChrClut` | 1 / 0 | [I] | Walks the unit chr's CLUTs (sub_83C9A0 count, sub_83C9B0, sub_83C9D0, sub_83C9F0) and rewrites each entry as (entry + add) * mul per channel, clamped. |
| 500 | `0x92DE50` | `FFX_Yanagi_MtxScale001` | 1 / 0 | [V] | Identity matrix with 0.01 on the diagonal (0x92DE50). |
| 501 | `0x92DEB0` | `FFX_Yanagi_CopyChrClut` | 1 / 0 | [V] | sub_83C710 on the unit chr, host slot 345, which allocates a CLUT copy ("%s:CLUT"). |
| 502 | `0x92DF10` | `FFX_Yanagi_ReleaseChrClut` | 1 / 0 | [V] | FFX_Ch_ReleaseTexListACopy on the unit chr, the release for FFX_Yanagi_CopyChrClut. |
| 432 | `0x92DF90` | `FFX_Yanagi_EndCasterMotion` | 39 / 0 | [V] | sub_787950(caster dword_25D5BB8, 0), host slot 213, which resets the actor's motion state for mode 0. |
| 433 | `0x92DFB0` | `FFX_Yanagi_EffectSetReady` | 40 / 0 | [V] | FFX_Btl_EffectSetReady(g_ffxYanagiCtx +4, a1 != 0), the state 4 to 5 step from EFFECT_OVERLAY.md. 40 DLLs. |
| 477 | `0x92DFD0` | `FFX_Yanagi_GetUnitRot` | 4 / 0 | [V] | FFX_Btl_GetUnitRot(unit) wrapped to 0..2pi by FFX_Yanagi_WrapAngle2Pi. |
| 480 | `0x92E000` | `FFX_Yanagi_GetUnitBoneNode` | 3 / 0 | [V] | FFX_Ch_GetBoneNodeIndex(FFX_Yanagi_GetUnitChr(unit), bone, 0). |
| 476 | `0x92E020` | `FFX_Yanagi_GetJointDirFromRoot` | 1 / 0 | [V] | Joint a2 and root joint world matrices of the unit, the difference scaled by 10 into a3 (sub_92D720, sub_92D690). |
| 474 | `0x92E0A0` | `FFX_Yanagi_TransformByJoint` | 7 / 0 | [V] | Scales point a3 by 1000, w = 1, and transforms it by the unit's joint a2 world matrix into a4. |
| 488 | `0x92E110` | `FFX_Yanagi_GetUnitHeight` | 2 / 0 | [V] | (bone 0 y - bone 2 y) * 10 from FFX_Ch_GetBoneWorldPos on the unit chr. |
| 503 | `0x92E170` | `FFX_Yanagi_GetRootDir` | 1 / 0 | [V] | The unit's root joint matrix (sub_92E270), one row copied out and normalised. |
| 481 | `0x92E1C0` | `FFX_Yanagi_GetUnitChr` | 5 / 0 | [V] | In battle (FFX_Btl_GetPhase != 0) FFX_Btl_GetUnitChr(id), otherwise FFX_Atel_GetPartyCharChr(id), which here works as a chr lookup. Used by 19 exe functions in this file. |
| 463 | `0x92E270` | `FFX_Yanagi_GetBoneMatrixScaled` | 8 / 0 | [V] | sub_92E240 on the bone node of a2: the joint world matrix with each row scaled by 100 (sub_92E630). |
| 456 | `0x92E2F0` | `FFX_Yanagi_GetJointPos` | 10 / 0 | [V] | Joint a2 world matrix of the unit chr, translation row copied to a3. |
| 457 | `0x92E340` | `FFX_Yanagi_GetBonePos` | 12 / 0 | [V] | FFX_Yanagi_GetJointPos on the bone node of bone a2. |
| 513 | `0x92E360` | `FFX_Yanagi_GetBonePosGround` | 3 / 0 | [V] | FFX_Yanagi_GetBonePos with y zeroed. |
| 489 | `0x92E3A0` | `FFX_Yanagi_GetUnitRadius` | 2 / 0 | [V] | FFX_Ch_GetDataCollisionRadiusScale of the unit chr times 10. |
| 514 | `0x92E400` | `FFX_Yanagi_AngleDiff` | 3 / 0 | [V] | /a - b/ folded into 0..pi (0x92E400). |
| 469 | `0x92E4F0` | `FFX_Yanagi_RandDirXZ` | 1 / 0 | [V] | Random unit vector in the xz plane from two CRT rand() draws, retried until non zero. A DLL reachable CRT rand caller (magic_0195). |
| 482 | `0x92E580` | `FFX_Yanagi_Lerp` | 6 / 0 | [V] | a + (b - a) * f(t), f linear, mode 1 a sine ease in out and mode 2 a sine ease out, through g_ffxSinTable4096. |
| 504 | `0x92E630` | `FFX_Yanagi_MtxScaleRows100` | 1 / 0 | [V] | Scales the first three rows of a matrix by 100. |
| 444 | `0x92E680` | `FFX_Yanagi_PlaySound` | 39 / 0 | [V] | Sub table 6 entry a1 of the Yanagi context's Oef2 unit, then sub_7FFF80 and sub_7FFE10(entry, 64, 256), host slot 220, which plays a sound or a streamed battle sound. |
| 445 | `0x92E6B0` | `FFX_Yanagi_PlaySoundIfIdle` | 8 / 0 | [V] | As FFX_Yanagi_PlaySound but only when FFX_SpuStream_PollRequest returns 0. |
| 483 | `0x92E730` | `FFX_Yanagi_ClearChrMatrixOverride` | 3 / 0 | [V] | FFX_Ch_SetI10C(unit chr, 0), clears the override that FFX_Yanagi_SetChrMatrixOverride sets. |
| 459 | `0x92E750` | `FFX_Yanagi_StartUnitTurn1` | 3 / 0 | [V] | sub_795EB0(unit, 1, 0, 0, a2, a3), host slot 537, the battle unit turn or move kind 1 setup the AtelSys Btl StartTurnKind syscalls use. |
| 472 | `0x92E770` | `FFX_Yanagi_ReverseScreenFade` | 9 / 0 | [V] | sub_925530(&g_ffxYanagiCtx): swaps the fade's from and to colours and restarts it. |
| 524 | `0x92E780` | `FFX_Yanagi_EndUnitMotion` | 1 / 0 | [V] | sub_787950(unit, 0), the FFX_Yanagi_EndCasterMotion call for any unit. |
| 521 | `0x92E7A0` | `FFX_Yanagi_ReverseTargets` | 0 / 1 | [V] | Reverses the target id list dword_25D5BC0 in place (count dword_25D5BBC). |
| 484 | `0x92E830` | `FFX_Yanagi_SetChrMatrixOverride` | 3 / 0 | [V] | Copies matrix a2, scales its rows by the chr's FFX_Ch_GetScale times FFX_Ch_GetDataF30, and installs it with FFX_Ch_SetI10C (into a3, or the static dword_193C090 when a3 is null). |
| 460 | `0x92E960` | `FFX_Yanagi_StartUnitTurn2` | 3 / 0 | [V] | sub_795EB0(unit, 2, 0, a2, a3, a4), host slot 537 kind 2. |
| 507 | `0x92E990` | `FFX_Yanagi_SetUnitShowVote` | 1 / 0 | [V] | sub_796660(a1, a2), host slot 296, FFX_Btl_SetUnitShowVote(a1, 2, a2). |
| 446 | `0x92E9A0` | `FFX_Yanagi_ApplyPendingHits` | 36 / 0 | [V] | sub_78D980(g_ffxYanagiCtx +4, a1, a2), host slot 239, the same hit application AtelSys Btl_014_ApplyPendingHits runs. 36 DLLs. |
| 473 | `0x92E9C0` | `FFX_Yanagi_StartScreenFade` | 10 / 0 | [V] | Converts four colour bytes to floats and starts the Yanagi context's screen fade to that colour over a5 frames (sub_9255D0). |
| 485 | `0x92EA40` | `FFX_Yanagi_StartScreenFadeFromTo` | 3 / 0 | [V] | As FFX_Yanagi_StartScreenFade with an explicit from colour (sub_925630). |
| 447 | `0x92EB10` | `FFX_Yanagi_SetUnitMotion` | 28 / 0 | [V] | sub_7882C0(unit, a2), which sets the battle actor motion (FFX_Mot_SetByModeIndex, FFX_Ch_SetBlendFrames). FFX_Oef2_Op063_ChrMotion uses the same call. 28 DLLs. |
| 486 | `0x92EB20` | `FFX_Yanagi_StartUnitMove` | 5 / 0 | [V] | Forwards all args to FFX_Btl_StartUnitMove. |
| 508 | `0x92EB70` | `FFX_Yanagi_SetUnitWeaponVisible` | 1 / 0 | [V] | (unit, show): CHR of side record 0 via host sub_795B40(unit, 0) (the weapon record per BATTLE_ACTOR.md), then FFX_Ch_SetHideBit2(chr, show == 0). DLL 0361. |
| 491 | `0x92EBA0` | `FFX_Yanagi_StartScreenShake` | 1 / 0 | [V] | (mode, phaseStep, frames, amplitude, randRange): program +504..+532. Each step sub_924DF0 feeds FFX_Came_SetScreenOffset a sine table value times (amplitude + rand() % randRange), x or y by mode bit 0, decaying in mode >= 3. Uses CRT rand(). |
| 448 | `0x92EBF0` | `FFX_Yanagi_QueueStreamSound` | 8 / 0 | [V] | Sub table 6 entry a1, then sub_800390(entry, a2), which queues a type 2 sound entry in maybe_g_ffxSpuStreamRequest[a2 & 1]. |
| 522 | `0x92EC60` | `FFX_Yanagi_ShowText` | 4 / 0 | [V] | Sub table 5 entry a1 passed to sub_875FB0(7, a2, entry), the text overlay the AtelSys Core_491 ShowTextOverlay syscalls use. |
| 461 | `0x92EE40` | `FFX_YanagiTask_SetStateAndRerun` | 12 / 0 | [V] | FFX_YanagiTask_SetState then task flags /= 2. The task runner sub_92ED90 loops while bit 2 is set, so the new state runs in the same frame. 12 DLLs. |
| 441 | `0x92F970` | `FFX_YanagiTask_SetFollowJoint` | 20 / 0 | [V] | Sets task flags +188 bits 1..2 to 2, stores unit a2 at +624 and joint a3 at +628. |
| 442 | `0x92F9A0` | `FFX_YanagiTask_SetFollowBone` | 35 / 0 | [V] | FFX_YanagiTask_SetFollowJoint with the bone node of bone a3. 35 DLLs. |
| 505 | `0x9301E0` | `FFX_YanagiTask_ClearDrawFlags` | 1 / 0 | [V] | Clears bits 4..11 of task +188. |
| 443 | `0x930200` | `FFX_YanagiTask_SetDrawFlags` | 32 / 0 | [V] | FFX_YanagiTask_ClearDrawFlags then ORs a2 into task +188. 32 DLLs. |
| 487 | `0x930350` | `FFX_YanagiTask_WaitPartA` | 0, dead thunk | [V] | Only an unreferenced DLL thunk reaches this slot. Pops the task state (maybe_FFX_YanagiTask_PopStateFlag2) once part slot +816 is no longer live. |
| 471 | `0x9303C0` | `FFX_YanagiTask_WaitScreenFade` | 0, dead thunk | [V] | Only an unreferenced DLL thunk reaches this slot. Wait state: pops once dword_25D5C10 (program+496, the fade active flag) is 0. |
| 427 | `0x930430` | `FFX_YanagiTask_WaitUnitMotion` | 0 / 10 | [V] | Pops the task state once sub_787840 reports the unit's motion request done (-1). The unit is the per depth slot task[11 + 4 * depth]. |
| 495 | `0x930460` | `FFX_YanagiTask_WaitUnitMoveDone` | 0, dead thunk | [V] | Only an unreferenced DLL thunk reaches this slot. Pops the task state once actor +1045 (sub_7AAAC0) is 0. |
| 428 | `0x930490` | `FFX_YanagiTask_WaitParticleEnd` | 0 / 10 | [V] | Wait state: pops once bit 0 of task slot +640 is clear (sub_930010), the particle set by host 440 finished. Thunks in 10 DLLs. |
| 429 | `0x9304B0` | `FFX_YanagiTask_WaitEffectHold` | 0 / 10 | [V] | Pops the task state once FFX_Magic_GetEffectHoldFlag returns 0. |
| 462 | `0x930500` | `FFX_YanagiTask_WaitSpuStream` | 0, dead thunk | [V] | Only an unreferenced DLL thunk reaches this slot. Pops the task state once FFX_SpuStream_PollRequest returns 0. |
| 431 | `0x930520` | `FFX_YanagiTask_WaitSoleTask` | 0 / 11 | [V] | Pops the task state when the task is both dword_25D5A44 and dword_25D5AFC of the Yanagi context, the head and tail of its task list. |
| 465 | `0x930550` | `maybe_FFX_Yanagi_CreateChrAfterimage` | 1 / 0 | [I] | Allocates a ppp block for a chr copy of the unit (sub_8037C0 size), fills it through sub_803220 or sub_803360 and records the chr world matrix. Only magic_0529. |
| 466 | `0x930600` | `FFX_Yanagi_FreeChrAfterimage` | 1 / 0 | [V] | sub_930710 then FFX_Ppp_HeapFreeBlock on the block, the free for slot 465. |
| 467 | `0x930630` | `maybe_FFX_Yanagi_DrawChrAfterimage` | 1 / 0 | [I] | View x matrix, chr render setup (sub_831300, sub_8311D0, sub_830160), FFX_Ch_UpdateRenderForMagic573 and FFX_ChrInstance_SetMaterialColour from RGBA bytes +40..+43 / 128. |
| 468 | `0x930800` | `FFX_Yanagi_AllocChrAfterimage` | 1 / 0 | [V] | 256 byte ppp block initialised by sub_930770. Only magic_0529. |
| 760 | `0x25D5A20` | `g_ffxYanagiCtx` | copied by 581 | [V] | The Yanagi effect context. +4 is the effect handle FFX_Yanagi_EffectSetReady passes to FFX_Btl_EffectSetReady (0x92DFBC), +8 the Oef2 unit whose sub tables 5 and 6 hold text and sounds (0x92E689), +448 a screen fade that FFX_Yanagi_StartScreenFade starts and sub_924C70 steps, +0x198 the caster and +0x19C.. the target list. |

### battle units (76 slots)

**Correction 2026-10-29 (the Project Zanarkand overdrive port, PROGRESS 13.9).** "Ally slot units only"
and "an ally slot unit" in the rows below come from the old name of `0x793650`, which was
`FFX_Btl_IsAllyUnit` and is now `FFX_Btl_IsValidUnitIndex` (tools/apply_monster_ai_names.py). It returns
unit <= 30, every battle unit with the monsters 20..27 included. So `FFX_Btl_IsUnitKO 0x795780` (slot 640)
is true for any unit 0..30 whose +0xDCC is set, read at 0x79578E..0x7957AA, and the same widening applies
to every other row that says ally slot. The IDB comment on 0x795780 still says ally slot.

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 292 | `0x782AE0` | `FFX_Btl_GetBattlePoint` | 9 / 0 | [V] | Writes the battle point vec4 to a1: the script point Btl 165 SetBtlPoint stored at qword_112A8D0 when byte 1 of dword_112C9FC is set (0x7A772D), else sub_9087F0, a scaled field vec4 at flt_18DF02C. Callers FFX_Oef2_Op140 and two others. 9 DLLs call it. |
| 698 | `0x784FF0` | `FFX_Btl_IsCharSwitchBlocked` | 1 / 0 | [V] | Returns ply_rom row byte 16 bit 0x4 for a character (FFX_KernelTable_GetRow on g_ffxKernelTablePlyRom). The battle menu pages skip pushing the Switch entry when it is set (BATTLE_MENU.md). 1 DLL (0203). |
| 411 | `0x787840` | `FFX_Btl_IsUnitMotionDone` | 277 / 0 | [V] | Returns 0 while the unit has a CHR, a non zero applied motion speed (+0x400) and FFX_Mot_IsRequestActive is set, and logs (unit, motion id +0x3F0) into the debug wait word word_112CA08. Returns -1 otherwise. Same shape as Btl 008 WaitMotion. 277 DLLs call it. |
| 213 | `0x787950` | `maybe_FFX_Btl_ReleaseUnitMotion` | 288 / 0 | [I] | Ends a scripted unit motion: if sub_788270 cleared the +0x3F3 scripted motion flag it clears +0x412 (unless mode 3), sets per mode state bytes and runs the unit battle event 0 or 8 through sub_7AB360. Oef2 op 65 ChrSpeed modes 1 and 2. 288 DLLs call it, which fits returning the caster to idle. |
| 684 | `0x787C10` | `FFX_Btl_GetUnitAiProp225` | 9 / 0 | [V] | Returns the signed byte at BTLUNIT +0xDDA, the field FFX_BtlAi_GetProperty case 225 reads. Meaning not known. 9 DLLs call it. |
| 529 | `0x787C30` | `maybe_FFX_Btl_EffectReturnToIdle` | 158 / 0 | [I] | Takes a packed effect handle and acts only for kind 2 (battle unit): clears +0x405, ends the scripted motion (sub_788270) and, when +0xE01 (set by Btl_StartCommandExec) is not 0xFF, runs that unit battle event with the direction fix byte forced on. 158 DLLs call it with their stored handle from a state switch. |
| 228 | `0x7882C0` | `FFX_Btl_PlayUnitMotion` | 289 / 0 | [V] | Plays motion a2 on a battle unit: refuses without a CHR or when petrified, sets the scripted motion flag +0x3F3 and motion id +0x3F0, FFX_Mot_SetByModeIndex, hokan and blend frames unless bit 0x8000, returns the sequence frame count. Oef2 op 63 ChrMotion. 289 DLLs call it. |
| 688 | `0x7883F0` | `maybe_FFX_Btl_HoldUnitMotion` | 3 / 0 | [I] | For an ally slot unit sets the scripted motion flag +0x3F3 to 1 and the motion id +0x3F0 to -1, so the unit counts as script controlled with no motion picked (0x7883F0). 3 DLLs call it. |
| 530 | `0x788430` | `FFX_Btl_SetUnitMotionSpeedOverride` | 163 / 0 | [V] | a2 >= 0 stores a2 at +0x402 and sets the override flag +0x404, a2 < 0 clears the flag, then sub_78AC20 recomputes and applies the unit motion speed (FFX_Ch_SetSpeedMulA). Called by FFX_Btl_PlayUnitMotion. 163 DLLs call it. |
| 689 | `0x788DD0` | `FFX_Btl_GetUnit13PartFlag` | 2 / 0 | [V] | Returns byte_112CA06, the flag Btl 216 SetUnit13PartFlag writes and Btl 217 applies to the unit 13 CHR model part. Same body as the dead Btl 219 getter. 2 DLLs call it. |
| 168 | `0x788DE0` | `FFX_Btl_UnitIdPassthrough` | 338 / 0 | [V] | Returns its byte argument unchanged (0x788DE0). Callers store the result as the effect owner unit (FFX_Yanagi_InitProgram 0x925296, sub_7FEBA0 0x7FEC99). 338 DLLs call it with the caster unit at effect start. |
| 294 | `0x788E10` | `FFX_Btl_GetUnitSummoner` | 9 / 0 | [V] | Returns BTLUNIT +0x6D1 m_summoner, the unit that summoned this one, 0xFF none. Callers FFX_Oef2_Op100 and sub_7DBEE0. 9 DLLs call it. |
| 626 | `0x788E90` | `maybe_FFX_Btl_ResetUnitsForEffectStart` | 2 / 0 | [I] | For all 31 units: +0x412 = a1, clears the motion speed override +0x404, eases CHR m_anim378 to 0 over 30 frames (sub_796360) and, when a1 is set, clears flag 0x100 vote slot 2. FFX_Btl_StepMagicEffectStates calls it with the actor's +0x413 when an effect starts. 2 DLLs call it. |
| 388 | `0x78CA20` | `FFX_Btl_PlayUnitReaction` | 20 / 0 | [V] | Plays a hit reaction on the unit's link unit (+0x422): maps the reaction kind (BTLHIT.m_reaction) to a battle event number (12, 14, 15, 16, 9, 48, 27, 56 KO path) and runs it through sub_7AB360. Btl 215 PlayLinkReaction and FFX_Btl_ApplyPendingEffects call it. 20 DLLs call it. |
| 239 | `0x78D980` | `FFX_Btl_ApplyPendingHits` | 275 / 0 | [V] | Applies the staged hits of the unit's current command: FFX_Btl_ApplyPendingEffects on every unit in selector a2, sub_78EE80 on the rest, and kills self through FFX_Btl_OnUnitDefeated when nothing landed and the self KO flag is set. Btl 014 ApplyPendingHits and FFX_Oef2_OpApplyPendingHits. 275 DLLs call it. |
| 369 | `0x78F950` | `FFX_Btl_GetUnitStatus2` | 1 / 0 | [V] | Returns BTLUNIT +0x616, the status 2 word (Scan, distills, Shield, Boost, Eject, Auto-Life, Curse, Defend, Guard, Sentinel, Doom). 1 DLL (0020). |
| 240 | `0x790260` | `FFX_Btl_IsUnitMotionDone_Api` | 5 / 0 | [V] | One call wrapper: return FFX_Btl_IsUnitMotionDone(unit) (0x790260). Only DLLs reach it, 5 of them. |
| 696 | `0x790290` | `FFX_Btl_PlayUnitMotion_Api` | 2 / 0 | [V] | One call wrapper: return FFX_Btl_PlayUnitMotion(unit, motion) (0x790290). Only DLLs reach it, 2 of them. |
| 599 | `0x794280` | `FFX_Btl_GetUnitPlaceLayout` | 1 / 0 | [V] | Ally slot units only: returns BTLUNIT +0x4FC m_placeLayout, the layout part of the battle position key, else 0. 1 DLL (0250), right before FFX_Btl_SetUnitPlaceLayout. |
| 169 | `0x7948A0` | `FFX_Btl_GetUnitHomeFacing` | 10 / 0 | [V] | Ally slot units only: returns m_homeFacing[m_facingIndex] (+0x514 indexed by +0x511), else 0.0. Callers FFX_Oef2_Op072_UnitMove and FFX_Oef2_Op114. 10 DLLs call it. |
| 664 | `0x794990` | `FFX_Btl_GetUnitYOffset` (was `maybe_FFX_Btl_GetUnitYOffset`) | 116 / 0 | [V] | Confirms the maybe_ name. Returns BTLUNIT +0x534 m_yOffset when the sleep counter +0x608 is 0 and the unit is not KO (or +0xDE7 is set), or when +0x50D is set, else 0.0. Btl 192 GetUnitYOffset wraps it. 116 DLLs call it. |
| 366 | `0x794C20` | `FFX_Btl_GetUnitCenterPos` (was `maybe_FFX_Btl_GetUnitCenterPos`) | 117 / 0 | [V] | Confirms the maybe_ name. Unit position (sub_796EA0), then y -= height / 2 unless the unit is airborne (+0x505, set by Btl 265) or the whole field is (g_ffxBtlFieldAirborne), when the position already is the centre. Returns -1. 117 DLLs call it. |
| 532 | `0x794D70` | `FFX_Btl_GetUnitAiProp126` | 3 / 0 | [V] | Ally slot units only: returns BTLUNIT +0x41F, the byte FFX_BtlAi_GetProperty case 126 reads and only BtlAi_SetProperty writes. FFX_Oef2_Op161 branches on it. Meaning not known. 3 DLLs call it. |
| 533 | `0x794DB0` | `FFX_Btl_TestUnitAiProp125OrPetrified` | 1 / 0 | [V] | Ally slot units only: true when BTLUNIT +0x41E (AI property 125) is set or the unit is petrified (+0xDCE). FFX_Oef2_Op161 branches on it. 1 DLL (0449). |
| 703 | `0x794E80` | `FFX_Btl_GetUnitAiProp128` | 1 / 0 | [V] | Ally slot units only: returns BTLUNIT +0xE0C, the byte FFX_BtlAi_GetProperty case 128 reads and only BtlAi_SetProperty writes. Meaning not known. 1 DLL (0263). |
| 534 | `0x794F70` | `FFX_Btl_IsUnitAirborne` | 130 / 0 | [V] | Ally slot units only: returns BTLUNIT +0x505, the flag Btl 265 SetAirborneMask sets with ground mode 0 (AI property 26). The centre and top position getters switch on it. Callers FFX_Oef2_Op095, Op162. 130 DLLs call it. |
| 216 | `0x7950F0` | `FFX_Btl_GetUnitHeight` (was `maybe_FFX_Btl_GetUnitHeight`) | 13 / 0 | [V] | Confirms the maybe_ name. FFX_Ch_GetHeight(unit CHR) times m_scale[1] (+0x3A4), 0.0 without a CHR (0x7950F0). Used by the centre and top position getters and FFX_Btl_ResolveMoveTargetPos. 13 DLLs call it. |
| 390 | `0x795610` | `FFX_Btl_GetUnitChrId` | 65 / 0 | [V] | Ally slot units only: returns BTLCHR +0x4 m_chrId, the chr id FFX_Btl_QueueChrLoad loads, else 0. 65 DLLs call it. |
| 535 | `0x795640` | `FFX_Btl_GetUnitMoveArcMode` | 3 / 0 | [V] | Returns the signed byte BTLUNIT +0x420, the 0..3 arc mode Btl 028 SetMoveArcMode writes and FFX_Btl_BuildArcPath switches on. 3 DLLs call it. |
| 347 | `0x795660` | `FFX_Btl_GetUnitMotionEventBits` | 4 / 0 | [V] | Ally slot units only: returns BTLUNIT +0x39C m_motionEventBits, bit k set by FFX_Btl_OnMotionEvent for motion event k. Caller FFX_Oef2_Op112_ChrI408. 4 DLLs call it. |
| 536 | `0x7956A0` | `FFX_Btl_GetUnitAppliedMotionSpeed` | 1 / 0 | [V] | Ally slot units only: returns the word at BTLUNIT +0x400, the motion speed sub_78AC20 last applied to the CHR with FFX_Ch_SetSpeedMulA (0x78AD1A). 1 DLL (0165). |
| 217 | `0x7956E0` | `FFX_Btl_GetUnitPos` (was `maybe_FFX_Btl_GetUnitPos`) | 175 / 0 | [V] | Confirms the maybe_ name. Ally slot units only: fills a vec4 with the unit position through sub_796EA0 (CHR position, or the parent unit bone when attached), w = 1.0. Always returns 0. 175 DLLs call it. |
| 640 | `0x795780` | `FFX_Btl_IsUnitKO` | 1 / 0 | [V] | True for an ally slot unit whose BTLUNIT +0xDCC m_ko is set (0x795780). Target selector -25 uses it (MONSTER_AI.md). 1 DLL (0564). |
| 492 | `0x795840` | `FFX_Btl_GetUnitAiProp8` | 2 / 0 | [V] | Ally slot units only: returns BTLUNIT +0x63D, the byte AI property 8 sets. Caller FFX_BtlAi_GetProperty. Meaning not known. 2 DLLs call it. |
| 665 | `0x795980` | `FFX_Btl_GetUnitTopPos` (was `maybe_FFX_Btl_GetUnitCenterPosAlt`) | 116 / 0 | [V] | Was maybe_FFX_Btl_GetUnitCenterPosAlt. Unit position, then y -= height / 2 when airborne (+0x505 or the field flag) and y -= full height when grounded. With +Y down that is the top of the unit in both cases, where the centre getter gives the middle. 116 DLLs call it. |
| 251 | `0x795A20` | `FFX_Btl_GetUnitRadius` | 36 / 0 | [V] | Returns BTLUNIT +0x4E4 (the last Btl 178 script float) times m_scale[2], 0.0 without a CHR. FFX_Btl_ResolveMoveTargetPos mode 10 adds it for both units plus the attack offset to get the stop distance. 36 DLLs call it. |
| 219 | `0x795B40` | `FFX_Btl_GetUnitSideChr` | 149 / 0 | [V] | Returns the CHR of side record a2 (weapon or armour) of unit a1. When that record has none it is meant to fall back to the CHR of the a2-th unit attached to the unit, but the attached-units test reads +0x440 off the 912 byte side record (0x795B65), not the unit, so the fallback never runs for side records 0..60 (correction 2026-10-28, BATTLE_ACTOR.md 9.2). Callers include FFX_Oef2_Op100, Op133, Op152to153. 149 DLLs call it. |
| 702 | `0x795C90` | `FFX_Btl_AttachUnitToUnitBone` | 2 / 0 | [V] | Attaches unit a1's CHR to bone of unit a2's CHR with FFX_Ch_AttachToParentBone and moves the unit to that bone (FFX_Btl_SetUnitPos), or detaches it when a2 < 0. 2 DLLs call it (HELD_OBJECTS.md). |
| 600 | `0x795D40` | `FFX_Btl_SetUnitPlaceLayout` | 1 / 0 | [V] | Ally slot units only: writes a2 to m_placeLayout (+0x4FC) and to the saved copy +0x509, returns -1. Pairs with FFX_Btl_GetUnitPlaceLayout in DLL 0250. |
| 625 | `0x795DA0` | `FFX_Btl_SetFlag100VoteAll` | 2 / 0 | [V] | For all 31 units: FFX_Btl_SetFlag100Vote(unit, 2, a1), the vote slot 2 of the CHR flag 0x100 (no body collision). DLLs 0391 and 0448 call it next to the effect start reset. |
| 666 | `0x795DD0` | `FFX_Btl_SetUnitRot` | 8 / 0 | [V] | Ally slot units only: FFX_Ch_SetRot(unit CHR, radians), returns -1, or 0 without a CHR. Sets the facing only, not m_moveDir. 8 DLLs call it. |
| 170 | `0x795E20` | `FFX_Btl_SetUnitRotAndMoveDir` | 8 / 0 | [V] | Ally slot units only: FFX_Ch_SetRotAndMoveDir(unit CHR, radians), returns -1, or 0 without a CHR. Callers FFX_Btl_PlaceArrivedAeon, FFX_Btl_SetupWeaponChr, FFX_Oef2_SetUnitOrChrRot. 8 DLLs call it. |
| 537 | `0x795EB0` | `FFX_Btl_StartUnitTurn` | 5 / 0 | [V] | Starts a scripted direction change: kind, target mode, ramp and hold frames into +0x42B..+0x430, total m_b42E = hold + 2 * ramp, refused while the direction fix byte is set. Btl 003, 006, 087, 264, 274 StartTurnKind wrap it. 5 DLLs call it. |
| 576 | `0x795FF0` | `FFX_Btl_SetUnitHomeFacingOverride` | 2 / 0 | [V] | Ally slot units only: stores the facing a3 at +0x53C and the enable bit a2 & 1 at +0x510. sub_7958C0 (Btl 159, 195) then returns that facing instead of +0x49C. 2 DLLs call it. |
| 539 | `0x7960D0` | `FFX_Btl_SetUnitMoveArcMode` (was `maybe_FFX_Btl_SetUnitFlag1056`) | 1 / 0 | [V] | Was maybe_FFX_Btl_SetUnitFlag1056. Ally slot units only: writes BTLUNIT +0x420, the arc mode Btl 028 SetMoveArcMode also writes and FFX_Btl_BuildArcPath reads, returns -1. 1 DLL (0472). |
| 540 | `0x796110` | `maybe_FFX_Btl_SetUnitMotionHold` (was `maybe_FFX_Btl_SetUnitFlag1042`) | 2 / 0 | [I] | Was maybe_FFX_Btl_SetUnitFlag1042. Writes BTLUNIT +0x412 (AI property 93). sub_7AA160 only picks an idle motion while it is 0, and the motion release paths clear it, so it reads as a hold on the current motion. 2 DLLs call it. |
| 295 | `0x7962B0` | `FFX_Btl_SetUnitB183Request2` | 88 / 0 | [V] | One call wrapper: sub_7962D0(unit, 2, value), which runs FFX_Btl_SetChrB183Request slot 2 on the unit, its side records and its attached units. Caller FFX_Oef2_Op133. 88 DLLs call it. |
| 541 | `0x796480` | `FFX_Btl_SetUnitShadeAlpha` | 98 / 0 | [V] | Stores alpha v at BTLUNIT +0x40 and FFX_Ch_SetShadeAlpha(v, mode) on the unit CHR, both side record CHRs and recursively on attached units. 98 DLLs call it. |
| 296 | `0x796660` | `FFX_Btl_SetUnitShowVote2` | 215 / 0 | [V] | One call wrapper: FFX_Btl_SetUnitShowVote(unit, 2, value). Callers FFX_Oef2_Op151_ChrVisibility, FFX_BtlAi_SetProperty, FFX_Magic_FlushDeferredUnload. 215 DLLs call it. |
| 238 | `0x7966A0` | `FFX_Btl_SetUnitShowVote1` | 30 / 0 | [V] | One call wrapper: FFX_Btl_SetUnitShowVote(unit, 1, value). Callers FFX_Oef2_Op151_ChrVisibility, Btl 047 ResetUnit, FFX_Btl_PlaceArrivedAeon and others. 30 DLLs call it. |
| 542 | `0x7968E0` | `FFX_Btl_SetUnitPushWeightMode` | 2 / 0 | [V] | Ally slot units with a CHR: FFX_Ch_SetF500 (CHR m_pushWeight) to m_weight (+0x4DC) when a2 > 0, 0.0 when a2 == 0, -1.0 (immovable) when a2 < 0. Callers FFX_Btl_FinishActions, FFX_Btl_StartCommandExec. 2 DLLs call it. |
| 291 | `0x79A680` | `FFX_Btl_SumPendingHitAmounts` | 229 / 0 | [V] | For attacker a1's current command, sums the not yet applied hits (m_next..m_count) that target a2 holds in its two BTLHITLOG blocks: HP, MP and CTB amounts out through a3..a5, returns the hit count. Callers FFX_Oef2_Op030, Op175, FFX_Btl_BuildHitTargetList. 229 DLLs call it. |
| 618 | `0x79A760` | `FFX_Btl_GetNextPendingHitAmounts` | 2 / 0 | [V] | For attacker a1's current command, returns the pending hit count on target a2 and the HP, MP, CTB amounts of only the next hit (index m_next) of each matching BTLHITLOG block through a3..a5. 2 DLLs call it. |
| 622 | `0x79A860` | `FFX_Btl_GetHitLogReaction` | 10 / 0 | [V] | Finds target a2's BTLHITLOG block staged by attacker a1's current command and returns its byte +0x15, 0 when none. Dead Btl 228 GetHitReaction wraps it. 10 DLLs call it. |
| 367 | `0x79AA50` | `FFX_Btl_GetPendingStatusChanges` | 215 / 0 | [V] | Compares the pending hits of attacker a1 on target a2 with the target's live status words: status 1 gained/lost bits, status counter gained/lost bits, status 2 gained/lost bits out through a3..a8. Returns 1 when the pending HP damage reaches 0 HP or KO is set. 215 DLLs call it. |
| 580 | `0x79AD90` | `FFX_Btl_GetUnitAiProp267` | 2 / 0 | [V] | Tests bit 0x200 of BTLUNIT +0x5B8 m_monsterFlags, which FFX_BtlAi_GetProperty case 267 also reads. Meaning of the bit not known. 2 DLLs call it. |
| 493 | `0x79E7C0` | `FFX_Btl_GetUnitStatusFxMask` | 1 / 0 | [V] | Packs the statuses that carry a visual into a mask: 1 Zombie, 2 Petrify (+0xDCE), 4 Berserk, 8 Haste, 0x10 Slow, 0x20 Curse (status 1 at +0x606, counters +0x613/+0x614, status 2 +0x616, read at 0x79E7D3..0x79E818). Caller sub_79EFF0. 1 DLL (0361). |
| 566 | `0x79EAE0` | `FFX_Btl_SetAllUnitsChrEffectsShown` | 3 / 0 | [V] | For all 31 units with a CHR and +0x4C set: sub_7FBAB0 (show, the Core 462 path) when a1 is set, else sub_7FBA40 (hide, Core 463), through sub_79EA40. Returns how many units it touched. 3 DLLs call it. |
| 415 | `0x7A9A20` | `FFX_Btl_GetUnitMotionId` | 5 / 0 | [V] | Returns BTLUNIT +0x3F0, the motion id FFX_Btl_PlayUnitMotion stores. Callers sub_792F20 (battle debug line) and sub_7DBEE0. 5 DLLs call it. |
| 598 | `0x7AB010` | `FFX_Btl_StartPathMove` | 1 / 0 | [V] | Starts move kind 7 along path slot a1 (0..2, needs a point): move kind +0x415 = 7, frames, spline index, speed a4 (0 when the direction fix byte is set), saves the rotation. Btl 073 StartPathMove and 118 StartArcTimeline. 1 DLL (0243). |
| 597 | `0x7AB0B0` | `FFX_Btl_AddPathPoint` | 1 / 0 | [V] | Appends a point to path slot a2 (0..2, 32 deep): copies the explicit point when given, then resolves it with FFX_Btl_ResolveMoveTargetPos for unit a3 and kind a4. Btl 071, 072 and FFX_Btl_BuildArcPath. 1 DLL (0243). |
| 596 | `0x7AB140` | `FFX_Btl_ClearPath` | 1 / 0 | [V] | Resets the point count of path slot a1 (0..2) at dword_1136418. Btl 070 ClearPath and FFX_Btl_BuildArcPath. 1 DLL (0243). |
| 808 | `0x7B1B50` | `MsSetReel` | 10 / 0 | [V] | Wakka's slots overdrive: for 4 reels resolves the reel result into an ability (word_C4395E, FFX_Btl_ResolveAbilityId) and stages its hits on targets, random ones through FFX_Rand_Stream(19). Its own strings "REEL%d" and "Virtuos FIXME ... when calling MsSetReel" (printed with this function's time args when the unit is 4) give the name. 10 DLLs call it. |
| 288 | `0x7B6C50` | `FFX_Btl_BuildUnitListByKind` | 109 / 0 | [V] | Fills byte_112C876 with unit indices by list kind a1 (5 summoned, 8 present 0..17, 9 and 16 the live party order plus present units, 10 aeon order, 11 live enemies, 19 enemy slots with a type), pads with 0xFF, returns the list and the count through a2. 109 DLLs call it. |
| 368 | `0x7B6FB0` | `FFX_Btl_BuildTargetOrderWithAttached` | 2 / 0 | [V] | FFX_Btl_BuildTargetOrder for unit a1 copied into +0xD60, then every unit linked to those targets through attach parents (sub_7B7440) appended. Returns the list, count through a2. 2 DLLs call it. |
| 412 | `0x7B7170` | `FFX_Btl_BuildHitTargetList` | 7 / 0 | [V] | Builds +0xD80 of unit a1: each target from FFX_Btl_BuildTargetOrder repeated once per pending hit (FFX_Btl_SumPendingHitAmounts), max 64 ("list_over"). Shuffles with FFX_Rand_Stream(16) only when a3 != 0. 7 DLLs call it. |
| 617 | `0x7B7280` | `FFX_Btl_GetHitTargetList` | 2 / 0 | [V] | Returns the +0xD80 hit target list of an ally slot unit (else null) and its count byte_112CA0E through a2. 2 DLLs call it. |
| 407 | `0x7B72C0` | `FFX_Btl_SortTargetOrderAcrossLine` | 3 / 0 | [V] | Target order of unit a1 sorted by each target's signed distance (sub_7B6F10, stored at +0xF68) from the line between the centres of selectors -14 and -15, a3 picks the axis. Returns the list, count through a2. FFX_Yanagi_InitProgram calls it. 3 DLLs call it. |
| 627 | `0x7B7D00` | `maybe_FFX_Btl_GetTopAggroChar` | 2 / 0 | [I] | Returns the party character 0..6 that is in the party, has ply_rom flag 0x2 (+0x590), is not KO or petrified and is allowed on an airborne field, with the highest dword_112CABC counter, else -1. maybe_FFX_SaveData_AdjustAffection bumps that counter from damage staging and enemy target picking. 2 DLLs call it. |
| 635 | `0x7B7D70` | `maybe_FFX_Btl_ReturnUnitToField` | 1 / 0 | [I] | Undoes maybe_FFX_Btl_SendUnitOffField: when +0xDFF is set, m_placeGroup -= 3, clears +0xDFF, resets motion and hold, re-places the unit (sub_794EC0, sub_796220), refreshes status fx and runs battle event 50. 1 DLL (0397). |
| 636 | `0x7B7E20` | `maybe_FFX_Btl_SendUnitOffField` | 1 / 0 | [I] | When +0xDFF is clear: m_placeGroup += 3 (the same shift FFX_Btl_PartyFlee uses), +0xDFF = 1, copies the group to +0x50A, clears +0x436, sets byte 1 of dword_112CA0F and runs battle event 18. 1 DLL (0397). |
| 637 | `0x7B7E80` | `FFX_Btl_IsUnitBusy` | 1 / 0 | [V] | True while BTLUNIT +0x432 is set, the busy flag Btl 049 SetBusy raises and Btl 062 WaitBattleIdle waits on. 1 DLL (0397). |
| 592 | `0x7B7F20` | `FFX_Btl_GetPendingGilCost` | 2 / 0 | [V] | Returns g_ffxBtlPendingGilCost (0x7B7F20). 2 DLLs call it (0018, 0019). |
| 523 | `0x7B7F30` | `FFX_Btl_SetSharedIntSlot` | 11 / 0 | [V] | dword_1136B18[a1] = a2 for a1 0..15. Only the dead Btl 198 getter reads the table back. 11 DLLs call it. |
| 389 | `0x7B7F50` | `FFX_Btl_SetSharedFloatSlot` | 5 / 0 | [V] | flt_1136AD8[a1] = a2 for a1 0..15. Only the dead Btl 199 getter reads the table back. 5 DLLs call it. |
| 919 | `0x1133398` | `g_ffxMagicSpecialActorList` | copied by 581 | [V] | [0] count then actor ids. sub_7A8C80 0x7A8DC7 marks a matching actor (+1011 = 1), only while magic 434 or 610 is loaded. No exe code writes it, the two DLLs fill it. |

### Oef2 interpreter (26 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 270 | `0x7EA120` | `FFX_Oef2_SplineEvalVec3` | 7 / 0 | [V] | Evaluates a cubic spline per component at time a2: copies each of x, y, z from the a5 key vec4s into a scratch buffer (Oef2 scratch stack 0x119FE10) and calls sub_7E9E20 with the knot times a4 and second derivatives. Callers FFX_Oef2_Move10, Op134. 7 DLLs call it. |
| 735 | `0x7F5F70` | `maybe_FFX_Oef2_DllOp_DrawTexMeshOrBranch` | 0 / 581 | [I] | Oef2 op handler reached from the DLL op table at header +0x7C. Mode 0x1000 draws a textured mesh for the object under dynamic geometry key 154 via sub_7F65D0 (skipped on catch up). Mode 0x2000 branches by the s16 at ip+2 unless sub_788DD0 (byte_112CA06, Btl 216) is set (0x7F5F70). |
| 911 | `0x7FA6D0` | `FFX_Oef2_DllOp_ScreenWarpMesh` | 0 / 581 | [V] | Oef2 op handler in the DLL op table at header +0x7C. Mode 0x1000 builds a textured screen mesh (key 706/2) from sub table 2 entry ip+2 via sub_7FA800. Mode 0x2000 builds a jittered FrameBuffer screen mesh (key 706/1) scaled by obj+0x40 via sub_7FA750. Returns ip+6 or ip+4. |
| 308 | `0xA65750` | `op_md_draw` | 4 / 0 | [V] | Dispatches a model draw on flag bits of *a2 (0x2000, 4, 0x80, 0x300) to op_md0_onef and five sibling draw bodies. In the op_md file between e_weapon_shadow and op_md_copy. |
| 567 | `0xA68C90` | `maybe_op_md_draw_hier` | 7 / 0 | [I] | For each node of a model list scales the node translation by 1000, multiplies by the parent matrix (sub_7E83B0) and the view (g_ffxEffectViewMatrix default), draws, then scales back. Callers FFX_Oef2_Op137, FFX_Opp_Kind14, Kind15. |
| 360 | `0xA68E10` | `maybe_op_md_draw_hier_alt` | 0 | [I] | No magic DLL calls this slot. The 0xA68C90 body with an extra matrix and a y nudge for magic 437 and 438. |
| 697 | `0xA69300` | `op_md_draw_simple` | 1 / 0 | [V] | Fills a default draw descriptor (flags 1052, size 136, colour 0x80808080, texture a3) and calls sub_A6B710, one of the op_md_draw bodies. |
| 620 | `0xA69DE0` | `maybe_op_md_set_packet_refs` | 1 / 0 | [I] | Unless magic 148, 569 or 208, walks the variable length records from a1 +32 (stride dword_C88AA8[type] * count + 16) and stores a2 and a3 at +8 and +12 of each. |
| 361 | `0xA6A3A0` | `maybe_op_md_push_draw_state` | 1 / 0 | [I] | Bumps byte_2332EA1, takes the next 6144 byte frame from dword_119FE0C and copies a 0x50 byte state from a2 into it, blending colours with sub_A68FF0. |
| 739 | `0xA6DBE0` | `maybe_u2_part_line_model` | 0 / 581 | [I] | Oef2 sub command (+0x7C table index 9 in magic_0021), phase from work+538 & 0xF000: 0 allocs and inits, 0x1000 steps, 0x2000 builds line prims (sub_A6E2E0, FFX_Vfx_CreateLineModelParts), 0x4000/0x5000 model trail buffers, 0x6000 a textured VFX model (sub_A6E650). Thunks in 581 DLLs. |
| 736 | `0xA6F4D0` | `maybe_u2_part_chr_shader` | 0 / 581 | [I] | Oef2 sub command (+0x7C index 7). Phase 0x1000 drives the effect CHR's render instance (sub_642B10, sub_6439B0, sub_63B810), otherwise draws the bound omd per node through 0xA68E10 and sub_643770. Thunks in 581 DLLs. |
| 740 | `0xA70910` | `maybe_u2_part_chr_break` | 0 / 581 | [I] | Oef2 sub command (+0x7C index 11). Phase 0x4000 captures the effect CHR geometry (host 704 sub_803220) into fragments (sub_A71780), 0x5000 draws them through sub_A70D70 whose log reads "opus_0582_break_draw", other phases sub_A72380, sub_A72B80, FFX_ScreenFade_RequestMode5. Thunks in 581 DLLs. |
| 728 | `0xA73810` | `maybe_u2_part_model_draw` | 0 / 581 | [I] | Oef2 DLL sub-command handler that walks the model list at +168 and builds the model unless g_ffxIsCatchUpStep. |
| 730 | `0xA73D90` | `maybe_u2_part_wave_model` | 0 / 581 | [I] | Oef2 sub command and DLL particle operator (+0x7C index 16, +0x78 index 2). Advances per vertex values with the sine tables flt_C43BF0/flt_C44BF0 in scratch, builds and submits a VFX model, frees it with FFX_DynGeoModel_DestroyGroupByKey. Thunks in 581 DLLs. |
| 729 | `0xA74410` | `maybe_u2_part_model_init` | 0 / 581 | [I] | Oef2 DLL sub-command handler, mode 0: allocates the part work from sub table 4 (op_malloc), sets +187 = 0x82 and builds the model geometry (FFX_Vfx_BuildModelGeometry). |
| 732 | `0xA74C60` | `maybe_u2_part_big_work` | 0 / 581 | [I] | Oef2 DLL sub-command handler, mode 0 allocates 163360 bytes of part work and initialises it (sub_A753F0), mode 0x1000 steps it (sub_A75170). |
| 733 | `0xA74D30` | `maybe_u2_part_texture` | 0 / 581 | [I] | Oef2 DLL sub-command handler, mode 0x3000 decodes a TEX0 style word and uploads a texture (sub_7E4420, sub_6438C0). |
| 734 | `0xA756E0` | `maybe_u2_part_chr_center` | 0 / 581 | [I] | Oef2 DLL sub-command handler measuring the effect chr bounds (sub_63A2B0) and setting its "center" bone offset (sub_63D880). |
| 737 | `0xA76720` | `maybe_u2_part_reset192` | 0 / 581 | [I] | Oef2 DLL sub-command handler, mode 0 clears +192 of the linked object, mode 0x1000 runs sub_A76770. |
| 738 | `0xA76B60` | `maybe_u2_part_chr_light` | 0 / 581 | [I] | Oef2 sub command (+0x7C index 8). Phase 0x1000 takes the effect CHR world matrix and its inverse and writes two vectors into the render instance (sub_63B5D0 slots 0 and 1), phase 0x2000 walks sub table 4 (sub_803200). Light or shader params inferred. Thunks in 581 DLLs. |
| 747 | `0x119FE90` | `g_ffxBattleEffectUnit` | copied by 581 | [V] | The current battle effect unit. sub_7FB250 frees the previous one (sub_7FDF70 at 0x7FB25D) and stores the new one, op_et_battle_effect_free 0x7FB170 clears it. |
| 750 | `0x11A0060` | `g_ffxEffectSysFlags` | copied by 581 | [V] | 0x40 byte effect system flag block, zeroed by sub_7FF960 (0x7FF9BC), which then sets +3 = 0x101 and +6 = 1. +1 is g_ffxEffectHoldFlag (FFX_Oef2_OpEffectSync 0x8166B9), +3 is cleared by FFX_Rand_SeedCosmeticStreams. |
| 749 | `0x22FB450` | `g_ffxMagicDllSharedBlock` | copied by 581 | [M] | No code in FFX.exe references it or the 0x80 bytes after it (xref scan of +0..+0x40, only the table entry 0xC6589C). A block only the DLLs use through the slot. |
| 746 | `0x2321540` | `g_ffxEffectRgSettings` | copied by 581 | [V] | 0xF00 byte effect debug settings block, named after the effect.rg file it loads from. sub_A63B00 zeroes it (0xA63B26) and fills it from the PS2 dev file host0:/home/%s/ffxprog/lib/effect.rg, which does not exist on PC, so retail leaves it zero apart from word +0x44 = 512. Read by 0x7FCE83, 0x810279 and sub_7FC2E0. |
| 748 | `0x2322780` | `g_ffxEffectDebugState` | copied by 581 | [V] | 0xA4 byte effect debug state, zeroed by sub_7FF960 (0x7FF979). Byte 0 is toggled by a NOT at 0x7FD0E0 and byte 0 AND byte 1 freezes effect time in opp_main (0x80BDB3, ctx+530). |
| 742 | `0x2332E8C` | `g_ffxEffectPacketCursorA` | copied by 581 | [V] | Effect work memory cursor. Reset to the base dword_2332E90 (set to 0x11A1260 by 0x7EC8A9) every step at 0x7FF73D, stored into effect unit 0 +84 before the resident Oef2 dispatch and read back after it (FFX_MagicResident_OvlStep 0x80062D, 0x800659). The end bound dword_2332E88 is base + 0xF0000. DLLs copy its value, not its address, at init. |

### stubs (24 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 854 | `0x0` | `- (left)` | 0 | [V] | The slot is a null pointer in FFX.exe itself (file read at 0xC65A40). No magic DLL reaches it. |
| 421 | `0x62F4C0` | `maybe_FFX_DebugHook_Stub` | 0 / 3 | [I] | Empty function with 119 exe callers, mostly loaders (FFX_Ev_LoadEventPackage, FFX_Field_MapRegLoadTask). Sits between _WinMain and FFX_MemZero. The callers suggest a stripped debug or profiling hook. |
| 727 | `0x62F510` | `maybe_FFX_DebugHalt_Stub` | 130 / 0 | [I] | Empty function right after SG_Printf_STUB. DLLs call it with 1 right after a debug print when two values disagree (magic_0021 at 0x1000117D), so it reads as a stripped halt or assert. |
| 424 | `0x62F7B0` | `maybe_FFX_Gs_ImageSync_Stub` | 2 / 0 | [I] | Returns 0. Sits between the two GS LoadImage stubs (423, 425). magic_0662 calls it with (buffer, value) right after a slot 423 upload of the same buffer, and FFX_SgCapture_WriteBmp is an exe caller, so it is a stripped PS2 GS transfer step. |
| 673 | `0x7E6B20` | `maybe_FFX_Ps2_FlushCacheStub` | 28 / 0 | [I] | Empty function. The exe calls it once in Oef2 packet build code (sub_A6B710 0xA6BA1D) and once at 0x80FFC8, the DLLs right before they publish a built buffer pointer, which fits a PS2 cache or DMA sync left as a stub. 28 DLLs call it. |
| 252 | `0x7EAAC0` | `maybe_FFX_Vfx_AppendScreenFillPacket_stub` | 2 / 0 | [I] | Returns 0 (0x7EAAC0). DLLs 0445 and 0645 call it as (packet, rgba, alpha byte, 0x80) and store the result back as their packet cursor, close to what FFX_Vfx_DrawScreenColorFill takes, so a PS2 packet builder compiled out on PC. |
| 581 | `0x7EC420` | `maybe_FFX_Ps2_DmaSendPacket_stub` | 13 / 0 | [I] | Empty on PC. Every caller pushes (packet start, size >> 4 in qwords, flags, 0), for example pppKeShpTailPht_Draw 0x754F0C with g_ffxVfxPacketCursor and DLL 0042 with 0x1000, so a PS2 DMA kick compiled out. 13 DLLs. |
| 563 | `0x7EC430` | `maybe_FFX_Ps2_DmaSendPacketAlt_stub` | 3 / 0 | [I] | Empty on PC. Same (packet, qword count, 0, 0) shape as nullsub_97 in DLLs 0042, 0374, 0609, and sub_A693B0 0xA69812 pushes (unk_22FB1C8, 14, 0). A second PS2 packet send routine compiled out. |
| 224 | `0x7FCBD0` | `maybe_FFX_Effect_EmptyHook` | 0 / 1 | [I] | Empty. Only DLL 0184 reaches it, through a thunk at 0x10003230 called as (0, 0, -1) at 0x1000176F. Sits in the effect API file between op_et_prayeff_send_read_through and op_event_effect_free. |
| 817 | `0x80A460` | `maybe_FFX_Effect_PostFreeNop` | 2 / 14 | [I] | Empty, in the FFX_Oef2_Op144 file. DLLs 0148 and 0569 call it right after a slot 804 free at 0x10001892, 14 more reach it by thunk. A PS2 era hook compiled out. |
| 816 | `0x80A4A0` | `maybe_FFX_Effect_PostInitUnitNop` | 0 / 19 | [I] | Empty, in the FFX_Oef2_Op144 file. 19 DLLs tail jump to it after FFX_Effect_InitUnit (slot 715) set up a unit, for example magic_0148 0x10001707. A PS2 era hook compiled out. |
| 577 | `0x909D30` | `FFX_HostApi_UnusedSlot577` | 0 | [V] | No magic DLL calls this slot. Forwards eight args to sub_912270(&g_ffxMapWork, ...), passing the last one only in debug mode. |
| 578 | `0x909D70` | `FFX_HostApi_UnusedSlot578` | 0 | [V] | No magic DLL calls this slot. Forwards to sub_911FC0(&g_ffxMapWork, ...). |
| 301 | `0x9DB190` | `FFX_MagicApi_ReturnZero` | 0 | [V] | No magic DLL calls this slot. return 0. |
| 767 | `0x9DB1A0` | `FFX_HostApi_UnusedSlot767` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 768 | `0x9DB1B0` | `FFX_HostApi_UnusedSlot768` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 805 | `0x9DB1C0` | `FFX_HostApi_UnusedSlot805` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 826 | `0x9DB1D0` | `FFX_HostApi_UnusedSlot826` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 866 | `0x9DB1E0` | `FFX_HostApi_UnusedSlot866` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 766 | `0x9DB1F0` | `FFX_HostApi_UnusedSlot766` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 806 | `0x9DB200` | `FFX_HostApi_UnusedSlot806` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 807 | `0x9DB210` | `FFX_HostApi_UnusedSlot807` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 851 | `0x9DB220` | `FFX_HostApi_UnusedSlot851` | 0 | [V] | No magic DLL calls this slot. An empty function. |
| 720 | `0x9DBEA0` | `FFX_MagicApi_EmptyHook` | 354 / 0 | [V] | An empty function that 354 DLLs call, so a hook point that does nothing on PC. |

### dyn geo (22 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 891 | `0x63FC30` | `FFX_VfxModel_GetBoundsMax` | 3 / 0 | [V] | Returns record+164, the bounds max FFX_Vfx_CreateModel sets to +10000 (0x644DE0). 3 DLLs. |
| 890 | `0x63FC40` | `FFX_VfxModel_GetBoundsMin` | 3 / 0 | [V] | Returns record+152, the bounds min FFX_Vfx_CreateModel sets to -10000 (0x644DE0). 3 DLLs. |
| 887 | `0x63FC50` | `FFX_VfxModel_GetPart` | 3 / 0 | [V] | Returns record+148 + 108*index, one 108 byte part (0x63FC50). 3 DLLs. |
| 888 | `0x63FC70` | `FFX_VfxModel_GetPartCount` | 0 | [V] | No magic DLL calls this slot. Returns record+144, the part count. |
| 900 | `0x63FC90` | `FFX_VfxPart_GetColours` | 3 / 0 | [V] | Returns part+20, the float4 colour array (DYNAMIC_MESHES part table). 3 DLLs. |
| 902 | `0x63FCA0` | `FFX_VfxPart_GetIndices` | 3 / 0 | [V] | Returns part+28, the u16 index array. 3 DLLs. |
| 896 | `0x63FCB0` | `FFX_VfxPart_GetIndexCount` | 0 | [V] | No magic DLL calls this slot. Returns part+8, the index count. |
| 899 | `0x63FCC0` | `FFX_VfxPart_GetWord16` (was `__fileno`) | 0 | [V] | No magic DLL calls this slot. The FLIRT match __fileno is a collision: the body is return *(a1+16), the same one line getter shape as its VfxPart neighbours 0x63FCB0 (+8) and 0x63FCD0 (+24), and the slot sits in their run. What part +16 holds is not traced. |
| 901 | `0x63FCD0` | `FFX_VfxPart_GetTexCoords` | 2 / 0 | [V] | Returns part+24, the float2 UV array. 2 DLLs. |
| 898 | `0x63FCE0` | `FFX_VfxPart_GetPositions` | 3 / 0 | [V] | Returns part+12, the float3 position array. 3 DLLs. |
| 894 | `0x63FCF0` | `FFX_VfxPart_GetVertexCount` | 0 | [V] | No magic DLL calls this slot. Returns part+4, the vertex count. |
| 897 | `0x63FD00` | `FFX_VfxPart_SetIndexCount` | 0 | [V] | No magic DLL calls this slot. Writes part+8. |
| 895 | `0x63FD10` | `FFX_VfxPart_SetVertexCount` | 0 | [V] | No magic DLL calls this slot. Writes part+4. |
| 903 | `0x63FD20` | `FFX_VfxPart_SetSamplerState` | 2 / 0 | [V] | Writes part+36, the sampler state index for sub_65E050 (DYNAMIC_MESHES part table). 2 DLLs. |
| 889 | `0x63FD30` | `FFX_VfxModel_SetPartCount` | 0 | [V] | No magic DLL calls this slot. Writes record+144. |
| 892 | `0x63FD50` | `FFX_VfxModel_SetZCorrect` | 3 / 0 | [V] | Writes record+128, the int zCrct that ClassDynamicGeometry__submit reads (0x6A4A50). 3 DLLs. |
| 886 | `0x63FDB0` | `FFX_DynGeoMem_AllocModelPart` | 8 / 0 | [V] | Allocates one block from DynGeoMem dword_CDEDF0 for a part's positions, colours, UVs, normals and indices, and writes the pointers and counts for the current set (0x63FDB0). Name from VISUAL_CENSUS. 8 DLLs. |
| 773 | `0x6453D0` | `FFX_Vfx_GetModelRecord` | 232 / 0 | [V] | Looks the u64 key up in registry dword_CCCA6C (sub_69F6E0) and returns geometry +36, the transform record with parts at +144/+148. 232 DLLs. |
| 884 | `0x645710` | `FFX_Vfx_SetModelLinked` | 3 / 0 | [V] | Looks the key up (sub_69F620) and links (sub_6A49E0) or unlinks (sub_6A2370) its mesh instance through FFX_Gfx_LinkMeshToNode. 3 DLLs. |
| 910 | `0x712BD0` | `FFX_Vfx_ReserveShpCounts` | 1 / 0 | [V] | Adds 6*n indices and 4*n vertices to part 0 of the model for shape a3 of the shp list a2 (0x712BD0). Used between reset and alloc in pppKeShpTail_Draw. 1 DLL. |
| 909 | `0x713290` | `FFX_Vfx_ResetModelPartCounts` | 1 / 0 | [V] | Zeroes every part's vertex and index count and the current set's copies (+44/+48 + 28*set) (0x713290). 1 DLL. |
| 937 | `0x713320` | `FFX_Vfx_ClearModelParts` | 4 / 0 | [V] | As 0x713290 but also zeroes each part's index buffer (+28, 2 * capacity +104) (0x713320). 4 DLLs. |

### vector and matrix math (22 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 326 | `0x62FD70` | `FFX_Vec3_LerpKeepW` | 44 / 0 | [V] | out.xyz = a*t + b*(1-t), out.w = a.w, returns b (0x62FD70). Next to FFX_Vec3_Dot. |
| 667 | `0x6ED3A0` | `FFX_Vec4_DivScalarSafe` (was `maybe_FFX_Vec4_DivScalarSafe`) | 116 / 0 | [V] | Body read and matches the guess. Was a maybe_ guess. |
| 553 | `0x6ED4F0` | `FFX_Mtx_FromRows` (was `maybe_FFX_Mtx_FromRows`) | 23 / 0 | [V] | Copies four row vectors into a matrix, body read and matches the guess. Was a maybe_ guess. |
| 565 | `0x6ED840` | `FFX_Mtx_TransformPointToFixed` | 135 / 0 | [V] | Transforms a vec3 point by a matrix through sub_705850 (w = 0). With a4 != 0 the float result is copied out, with a4 == 0 each lane is stored as int(v*16 + 0.5), the PS2 12.4 fixed point. |
| 685 | `0x75EDE0` | `FFX_Ppp_Vec4Copy` (was `PppMath__f75EDE0`) | 2 / 0 | [V] | Copies 16 bytes from a2 to a1, bouncing through the scratch globals at 0xC8F7A0 and 0xC8F7D0 (VU register emulation). Used by 22 ppp operators and the pppKeTh node code (0x764537). 2 DLLs call it. |
| 706 | `0x75FDA0` | `FFX_Ppp_Vec3Length` (was `PppMath__f75FDA0`) | 2 / 0 | [V] | Returns sqrt(x*x + y*y + z*z) of a float[3] (0x75FDA0). Callers are ppp shape tail and gravity operators. 2 DLLs call it. |
| 494 | `0x7E9A30` | `FFX_Oef2_RotTransMatrixYXZ` | 1 / 0 | [V] | a1 = a2 rotated about Y by a3[1] (sub_7E9670), then X by a3[0] (sub_7E9580), then Z by a3[2] (sub_7E9760), 4096 units per turn through flt_C44BF0, then a4 added to the translation row. Used by ppp model draws, rcbg FP and Oef2. 1 DLL (0361). |
| 269 | `0x7EA2D0` | `FFX_Spline_SetupCubicXyz` | 7 / 0 | [V] | Builds a cubic spline over n vec4 points: chord length params normalised to 0..1 into out[0..n), then sub_7E9F20 solves second derivatives per axis into out+n, +2n, +3n. Evaluator is host 270 0x7EA120. Callers FFX_Oef2_Op134, sub_813480. 7 DLLs. |
| 383 | `0x7EA700` | `FFX_Mtx_SetUniformScale` | 11 / 0 | [V] | Writes a 4x4 matrix with s on the diagonal of the 3x3 part and 1.0 at [15], rest zero (0x7EA700). Sits between the identity builder 0x7EA6D0 and the per axis scale 0x7EA730 in math.c. 21 exe callers, 11 DLLs. |
| 403 | `0x889B80` | `FFX_Math_DegToRadWrapped` | 10 / 0 | [V] | Degrees times pi/180, wrapped to [-pi, pi] with FFX_Math_WrapAngleToPiF, returned (0x889B97). 10 DLLs. |
| 453 | `0x921250` | `rcbgScene_Vec3Length` | 12 / 0 | [V] | sqrt(x*x + y*y + z*z) of a vec3 (0x921250). Sits in the rcbg scene file next to rcbgSceneResetShake, used by rcbgPSimCreate and rcfxSpringApply. |
| 512 | `0x9212C0` | `rcbgScene_Vec3Cross` | 1 / 0 | [V] | a1 = a2 x a3, the cross product (0x9212C0). |
| 464 | `0x921360` | `rcbgScene_Vec3Normalize` | 5 / 0 | [V] | Normalises a vec3 in place through rcbgScene_Vec3Length, zeroes all four lanes when the length is 0 (0x921360). |
| 554 | `0x93D550` | `FFX_Mtx_RotZYX` (was `maybe_FFX_Mtx_RotZYX`) | 220 / 0 | [V] | Applies sub_93D690 (Z, entries 0,1,4,5), then sub_93D620 (Y, 0,2,8,10), then sub_93D5A0 (X, 5,6,9,10) with angles a3[2], a3[1], a3[0]. Confirms the maybe_ name. 220 DLLs. |
| 395 | `0x93D830` | `FFX_Mtx_CopyAddTranslation` | 4 / 0 | [V] | Copies the 4x4 a2 into a1 and adds vec3 a3 to the translation row (0x93D830). |
| 297 | `0x9DB270` | `FFX_MagicApi_MtxMulVec4` | 5 / 0 | [V] | out = M * v for a 4x4 M (a2) and vec4 v (a3), staged through the scratch at 0xC8F940. 5 DLLs, among them 0515, 0611 and 0667. |
| 298 | `0x9DB490` | `FFX_MagicApi_MtxFillRows` | 4 / 0 | [V] | Writes the vec4 at qword_C8F508 into all four rows of a1. |
| 358 | `0x9DBAF0` | `FFX_MagicApi_Vec3NormalizeLen` | 1 / 0 | [V] | *a3 = /a1/, a2 = a1 / /a1/ (0x9DBAF0). |
| 241 | `0x9DBB40` | `FFX_MagicApi_EffectRandFloat` | 3 / 0 | [V] | Mixes g_ffxEffectRandState, re-seeds the CRT with srand(state & 0x7FFFFF) at 0x9DBB40, steps the state and returns a float in [1, 2) when g_ffxEffectRandInited. A third CRT re-seed path, MAGIC_DLL.md section 5. |
| 359 | `0x9DBD90` | `FFX_MagicApi_Vec4ScaleXYZ` | 2 / 0 | [V] | a1.xyz = a3.xyz * a2, a1.w = a3.w (0x9DBD90). |
| 300 | `0x9DBE10` | `FFX_MagicApi_Sqrt` | 4 / 0 | [V] | *a2 = sqrt(*a1). |
| 751 | `0xC44BF0` | `g_ffxSinTable4096` | copied by 581 | [V] | 4096 entry float sine table, entry 1 is 0.0015339 = sin(2pi/4096). FFX_Ppp_RotMatrixX 0x72C741 reads sin as [(a >> 4) & 0xFFF] and cos as [((a + 0x4000) >> 4) & 0xFFF], so a full turn is 0x10000. |

### app (21 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 939 | `0x641100` | `FFX_GetMasterVersion` | 1 / 0 | [V] | Returns dword 0 of the language manager (FFX_GetLanguage reads +4). core:595 IsWesternMasterVersion tests it against 0, 3, 4 (0x858210). 1 DLL. |
| 786 | `0x641160` | `FFX_App_GetFogColor` | 4 / 0 | [V] | Copies the application floats +96, +100, +104 out. sub_908980 (slot 558) scales them by 255 into the fog colour it returns when the magic fog override (g_ffxMagicDrawFlags bit 2) is off. |
| 789 | `0x6411A0` | `FFX_App_GetFogCurve` | 4 / 0 | [V] | Returns app+0x78. magic_0098 saves it and restores it through slot 784 (fog +40, the curve, FFX_Gfx_InstallFogFromGlobalCamera reads camera +236 m_fogCurve). 4 DLLs. |
| 788 | `0x6411B0` | `FFX_App_GetFogFar` | 4 / 0 | [V] | Returns application float +116. Exe caller sub_908BB0 (slot 559). |
| 790 | `0x6411C0` | `FFX_App_GetFogLimit` | 302 / 0 | [V] | Returns application float +124. Exe caller sub_908BB0 (slot 559). |
| 787 | `0x6411D0` | `FFX_App_GetFogNear` | 4 / 0 | [V] | Returns application float +112. Exe caller sub_908BB0 (slot 559), the fog near, far and limit getter. |
| 792 | `0x641230` | `FFX_App_GetMagicFogColor` | 1 / 0 | [V] | Copies the application floats +80, +84, +88 out. sub_908980 (slot 558) uses these instead of +96 when g_ffxMagicDrawFlags bit 2 is set. Slot 791 writes them. |
| 799 | `0x641270` | `FFX_App_GetFloat216` | 0 | [V] | No magic DLL calls this slot. Returns application float +216. |
| 798 | `0x641280` | `FFX_App_GetFloat212` | 0 | [V] | No magic DLL calls this slot. Returns application float +212. |
| 800 | `0x641290` | `FFX_App_GetFloat220` | 0 | [V] | No magic DLL calls this slot. Returns application float +220. |
| 797 | `0x6412A0` | `FFX_App_GetFloat208` | 0 | [V] | No magic DLL calls this slot. Returns application float +208. |
| 781 | `0x643150` | `FFX_Fog_SetColor` | 4 / 0 | [V] | sub_6E36F0 on the fog object (sub_6E32C0): writes rgb to its params +16..+24 and refreshes the cached copy. Exe callers are the AtelSys MapFx 062..065 SetFogColor syscalls. |
| 784 | `0x643180` | `FFX_Fog_SetCurve` | 4 / 0 | [V] | sub_6E3760 writes fog src+40. FFX_Gfx_InstallFogFromGlobalCamera fills +40 from camera +236, which is PCamera m_fogCurve. MapFx 055 passes 1.0. 4 DLLs. |
| 783 | `0x6431A0` | `FFX_Fog_SetFar` | 4 / 0 | [V] | sub_6E3A30 on the fog object. Exe callers FFX_AtelSys_MapFx_055_SetFog, FFX_AtelSys_MapFx_058_SetFogFar, sub_90A350. |
| 785 | `0x6431C0` | `FFX_Fog_SetLimit` | 345 / 0 | [V] | sub_6E3AA0 on the fog object. Exe callers FFX_AtelSys_MapFx_055_SetFog, FFX_AtelSys_MapFx_060_SetFogLimit, sub_90A350. |
| 782 | `0x6431E0` | `FFX_Fog_SetNear` | 4 / 0 | [V] | sub_6E3B10 on the fog object. Exe callers FFX_AtelSys_MapFx_055_SetFog, FFX_AtelSys_MapFx_056_SetFogNear, sub_90A350. |
| 791 | `0x643270` | `FFX_App_SetMagicFogColor` | 116 / 0 | [V] | Writes the application floats +80, +84, +88, the fog colour sub_908980 reads under the magic override. |
| 795 | `0x6432C0` | `FFX_App_SetFloat216` | 0 | [V] | No magic DLL calls this slot. Application float +216 = a1. |
| 794 | `0x6432E0` | `FFX_App_SetFloat212` | 0 | [V] | No magic DLL calls this slot. Application float +212 = a1. |
| 796 | `0x643300` | `FFX_App_SetFloat220` | 0 | [V] | No magic DLL calls this slot. Application float +220 = a1. |
| 793 | `0x643320` | `FFX_App_SetFloat208` | 0 | [V] | No magic DLL calls this slot. Application float +208 = a1. |

### effect objects and prims (21 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 677 | `0x7644D0` | `FFX_PppTh_SpawnNode` | 12 / 0 | [V] | pppKeTh node pool: pops a node off the free list at pool+4, inits it (sub_763030), sets node flags from the list byte +4, copies two vec4s (FFX_Ppp_Vec4Copy) and links it at the head of the live list a2. Called from pppKeTh_Calc 0x736221. 12 DLLs call it from their own Th operator copies. |
| 680 | `0x7645E0` | `FFX_PppTh_DrawNodes` | 12 / 0 | [V] | pppKeTh node list draw: skipped on catch-up steps, builds the proj and inverse camera matrices, walks the live list calling sub_763100 per node, then builds or destroys the dynamic geometry model for the op (FFX_Vfx_BuildModelGeometry, FFX_DynGeoModel_DestroyGroupByKey). Called by pppKeTh_Draw. 12 DLLs call it. |
| 678 | `0x764800` | `FFX_PppTh_InitList` | 10 / 0 | [V] | Clears a pppKeTh live list head: PRX pointer 0 at +0 and flag byte +4 = 0 (0x764800). Called by pppKeTh_Con. 10 DLLs call it. |
| 679 | `0x7649A0` | `FFX_PppTh_StepNodes` | 12 / 0 | [V] | pppKeTh per frame step: walks the live list, refreshes the two endpoint vec4s on flagged nodes, steps each node (sub_7637F0, 3295 bytes of segment math), and returns dead nodes (flag 0x10) to the pool free list. Called by pppKeTh_Calc 0x736432. 12 DLLs call it. |
| 676 | `0x764BB0` | `FFX_PppTh_InitNodePool` | 10 / 0 | [V] | Builds a pppKeTh node pool: count a2 nodes of 48*(a3-1)+128 bytes each after a 16 byte header, chained as a PRX pointer free list from header+4 (0x764BB0). Called by all 37 pppKeThRes*_Con operators. 10 DLLs call it. |
| 604 | `0x767790` | `FFX_Vfx_FrameBufferPatch_Draw` | 1 / 0 | [V] | Draws a screen space patch that samples the "FrameBuffer" texture: projects the point, culls by screen box (magic ids 106 and 445 widen it), creates the op model with a FrameBuffer part on first use, fills 9 vertex colours, positions and UVs. Always called right after FFX_Vfx_FrameBufferPatch_Setup (pppKeShpDtt_Draw 0x755390, FFX_Opp_Kind12). 1 DLL (0271). |
| 603 | `0x768D30` | `FFX_Vfx_FrameBufferPatch_Setup` | 1 / 0 | [V] | Loads the emulated VU state for FFX_Vfx_FrameBufferPatch_Draw: pushes a scratch matrix, copies the camera and param block vectors into the 0xC8F7A0..0xC8F9A0 globals and the 0.7071 corner offsets. Called right before the draw in pppKeShpDtt_Draw, pppKeShpTailPht_Draw and FFX_Opp_Kind12. 1 DLL (0271). |
| 352 | `0x7EAAD0` | `FFX_Vfx_DrawScreenColorFill` | 2 / 0 | [V] | Full screen colour fill: rgba plus blend mode byte (bit 0x20 swaps in the alpha arg), pushes pass command 2 via FFX_GfxCtx_PushPassCommand2UnlessCatchUp, then builds a dead PS2 GS packet. Magic 386 premultiplies rgb by alpha/128 (0x7EAAD0). Callers FFX_Oef2_Op077, sub_7EADB0. |
| 418 | `0x7EB350` | `FFX_Vfx_RemoveCallbackGroup0` | 1 / 0 | [V] | sub_7EB4A0(id, 0): unregisters a callback from group 0, the group FFX_Vfx_EndPass runs first (0x7EC486). Group 32 twin is FFX_Vfx_RemoveEffectCallback. |
| 417 | `0x7EB370` | `FFX_Vfx_AddCallbackGroup0` | 1 / 0 | [V] | sub_7EB500(fn, a, b, count, 0): registers a callback in group 0, which FFX_Vfx_EndPass runs before the draw batch mark (0x7EC486). Callers FFX_RcBg_Step, maybe_FFX_Oef2_OpRegisterHdrCallback. |
| 376 | `0x7EB3A0` | `FFX_Vfx_RemoveCallbackGroup64` | 6 / 0 | [V] | sub_7EB4A0(id, 64): unregisters a callback from group 64, run third by FFX_Vfx_EndPass (0x7EC4CA). 6 DLLs. |
| 377 | `0x7EB3C0` | `FFX_Vfx_AddCallbackGroup64` | 6 / 0 | [V] | sub_7EB500(fn, a, b, count, 64): registers a callback in group 64, run third by FFX_Vfx_EndPass after the free list rebuild (0x7EC4CA). 6 DLLs. |
| 381 | `0x7EC7D0` | `FFX_Vfx_SetRebuildNodeFreeList` | 6 / 0 | [V] | byte_11A0067 = (a1 != 0). FFX_Vfx_EndPass tests it at 0x7EC4A4 and, when set, relinks the 4131 node free list at unk_12A89AC. Callers op_ovl_effect_free, maybe_FFX_Oef2_OpRegisterHdrCallback. 6 DLLs. |
| 315 | `0x7EC7E0` | `FFX_Vfx_DrawScreenDepthQuad` | 10 / 0 | [V] | Maps a PS2 24 bit Z (0 -> 1.0) to a depth, then on a rotating dynamic geometry key 1..5 (qword_C48C10) fills a full screen quad at that depth (sub_71E330) and submits it (sub_645600), or builds the model first (sub_711680). Magic 154 marks a draw batch first. Caller FFX_Oef2_Op171. |
| 242 | `0x7ED0C0` | `maybe_FFX_Vfx_BuildEmulatedVuModelPacket` | 1 / 0 | [I] | 31 KB software emulation of a PS2 VU model microprogram: VU registers live at 0xC8F5xx..0xC8F9xx, takes (model data, draw block) and returns the new packet cursor, which DLL 0481 stores at block +0xB4 (0x100012FF). Only one DLL calls it. |
| 704 | `0x803220` | `FFX_Oef2_CaptureChrMeshAll` | 4 / 0 | [V] | Snapshots the skinned vertices of every CHR mesh into buffer a1: header (chr, count, mode a3), 16 byte part records with joint matrix pointer and visible bit, then vertex data (sub_830490), mode 2 adds per vertex extras (sub_A69F10). Special CHR kinds go through sub_80F460 (0x803220). FFX_Oef2_Op137. |
| 362 | `0x803360` | `FFX_Oef2_CaptureChrMeshVisible` | 30 / 0 | [V] | Same capture as FFX_Oef2_CaptureChrMeshAll but skips meshes FFX_Ch_ModelGetHide reports hidden (0x803360). Callers FFX_Oef2_ExplodeModel, FFX_Oef2_Op137. 30 DLLs. |
| 363 | `0x8037C0` | `FFX_Oef2_GetChrMeshCaptureSize` | 10 / 0 | [V] | Buffer size for the mesh capture: 16*(count+1) plus each mesh's 16 aligned vertex size, mode 2 adds 12 per 28 byte vertex, a3 limits it to visible meshes (0x8037C0). 10 DLLs. |
| 946 | `0x829C60` | `FFX_Vfx_ArmAlternateTexOnce` | 1 / 0 | [V] | If the CHR id word is 0x1154 or the battle id is 0x024B0006, sets dword_CEC198 = 1. sub_712EC0 consumes it once to load texture 15281 instead of 15280 (0x712F36). DLL 0434. |
| 867 | `0xA5BF50` | `FFX_Vfx_DrawModelScroll` | 2 / 0 | [V] | Creates the VFX model on first use (sub_A5C130, string "Virtuos Error: Have not load the vfx texture (ModelScroll %s"), builds geometry, fills the prims (sub_A5C370, skipped on catch up steps when cached) and FFX_Vfx_SubmitModel, else FFX_DynGeoModel_DestroyGroupByKey. DLLs 0165, 0606. |
| 850 | `0xCCCA58` | `g_ffxVfxDynGeoForceBlend4` | copied by 581 | [V] | When 1, VFXDynamicGeometry__submit 0x6A5325 forces blend mode 4 for any geometry whose mode is not 2. No exe code writes it, so only a DLL can set it. |

### vfx model draw (21 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 870 | `0x645600` | `FFX_Vfx_SubmitModelOrdered` | 9 / 0 | [V] | FFX_Vfx_SubmitModelMode(key, 0x80000000), mode 0 with the sign bit that selects the ordered list (DISPLAY_LIST section 7). 9 DLLs. |
| 871 | `0x710FD0` | `FFX_Vfx_DrawMdlOrdered` | 8 / 0 | [V] | Creates the Mdl model (sub_713790) and its geometry, queues the build with FFX_Vfx_QueueModelBuild (0x718610), submits with FFX_Vfx_SubmitModelOrdered. Destroys the key on failure. 8 DLLs. |
| 872 | `0x711030` | `FFX_Vfx_DrawShpOrdered` | 0 | [V] | No magic DLL calls this slot. Creates the Shp model (sub_715170, createVFXRenderDataBufferShp string), halves part index counts, FFX_Vfx_QueueSpriteBuild, FFX_Vfx_SubmitModelOrdered. |
| 874 | `0x7110D0` | `FFX_Vfx_DrawSingleSpriteOrdered` | 4 / 0 | [V] | Creates a one texture sprite model (sub_715810), divides index counts by dword_C3A484, FFX_Vfx_QueueSingleSpriteBuild (0x71C0B0), FFX_Vfx_SubmitModelOrdered. 4 DLLs. |
| 820 | `0x711180` | `FFX_Vfx_DrawMdl` | 113 / 0 | [V] | As 0x710FD0 but submits with FFX_Vfx_SubmitModel. The usual ppp DrawMdl path (RCFLEX.md). 113 DLLs. |
| 907 | `0x7113D0` | `FFX_Vfx_CreateShpBatchModel` | 5 / 0 | [V] | Creates a multi texture shape model sized for a4 copies (sub_714D40), divides index counts by 3. No build or submit, callers then reset, reserve and append per shape (pppKeShpTail_Draw). 5 DLLs. |
| 778 | `0x711460` | `FFX_Vfx_DrawShp` | 123 / 0 | [V] | As 0x711030 but submits with FFX_Vfx_SubmitModel. Used by pppDrawShape and opp kinds 1 and 9. 123 DLLs. |
| 873 | `0x711500` | `FFX_Vfx_DrawSingleSprite` | 7 / 0 | [V] | As 0x7110D0 but submits with FFX_Vfx_SubmitModel. Called by FFX_Opp_Kind10. 7 DLLs. |
| 860 | `0x7116D0` | `FFX_Vfx_DrawFrameBufferNoiseStrips` | 1 / 0 | [V] | Model key 6 on "D3D11FrameBuffer" (sub_716050), filled by sub_71DBD0 with rand placed strips sampling the frame buffer, tinted by four bytes, sets dword_CEC19C for FFX_Frame_RenderScene. 1 DLL. |
| 862 | `0x711750` | `FFX_Vfx_DrawFrameBufferGrid` | 2 / 0 | [V] | Pushes a frame buffer copy, creates a 33x33 grid model on "FrameBuffer" (sub_715A40, 1089 verts), fills UVs from a2 and colour a3 (sub_71CE20), submits in mode 4 (sub_644DB0). A screen warp. 2 DLLs. |
| 828 | `0x7117C0` | `maybe_FFX_Vfx_DrawFrameBufferMesh` | 2 / 0 | [I] | Skips catch-up steps and modes above 2. Builds a triangle mesh model (sub_715AF0) textured by its own TEX0 (mode 0), "FrameBuffer" (1) or 15040 (2), fills it through matrix a2 (sub_71CF90), submits. Role partly inferred. 2 DLLs. |
| 815 | `0x711830` | `maybe_FFX_Vfx_DrawMdlImmediate` | 10 / 0 | [I] | Key (a1, ppp op index). Creates a model from the mesh at a2+28 (sub_715DC0, blend from a3+8) and fills it at step with sub_71D520 instead of the prim build queue, then submits. 10 DLLs. |
| 830 | `0x712690` | `FFX_Vfx_AppendTriPair` | 1 / 0 | [V] | Appends one triangle to each of models (a1,0) and (a1,1) from the 3 vertex records at a2+16 and a2+160, counter dword_CEC194. With a2 null it links and builds or submits both (0x712690). 1 DLL. |
| 832 | `0x7128D0` | `FFX_Vfx_AppendUntexturedQuad` | 3 / 0 | [V] | Appends six vertices (two triangles) to model (a1,0) from a2, counter dword_CEC194. With a2 null it links and submits (0x7128D0). Pair of 0x713160. 3 DLLs. |
| 777 | `0x712D00` | `FFX_Vfx_BoolFromInt` | 229 / 0 | [V] | Returns a1 != 0. The pppDrawMdl draw columns call it. |
| 829 | `0x712EC0` | `FFX_Vfx_CreateTriPairModels` | 1 / 0 | [V] | Creates models (a1,0) on 15280 or 15281 (dword_CEC198) and (a1,1) on 13600, sized 3*a2, allocates DynGeoMem, returns both records, zeroes dword_CEC194. a2 0 destroys both. Pair of 0x712690. 1 DLL. |
| 831 | `0x713160` | `FFX_Vfx_CreateUntexturedQuadModel` | 3 / 0 | [V] | Creates model (a1,0) on "NoTexture" with 6*a2 vertices and indices, allocates DynGeoMem, zeroes dword_CEC194. a2 0 destroys it. Pair of 0x7128D0. 3 DLLs. |
| 819 | `0x718610` | `FFX_Vfx_QueueModelBuild` | 121 / 0 | [V] | Skipped on a catch-up step. Captures a model slot for the build job, sets the job built byte +193, and bakes colour scale and UV scroll. VISUAL_CENSUS names it the kind 0 prim queue producer. |
| 908 | `0x71A4E0` | `FFX_Vfx_AppendShpGeometry` | 5 / 0 | [V] | Skips catch-up steps. Appends one shape's quads into the batched model at the current counts (+4 += 4n, +8 += 6n) and sets sort depth. pppKeShpTail_Draw calls it per segment after 0x712BD0 and the alloc. 5 DLLs. |
| 774 | `0x71B8A0` | `FFX_Vfx_QueueSpriteBuild` | 123 / 0 | [V] | Fills kind 1 slots of the prim build queue for a shp list (run by FFX_Vfx_PrimBuild_Sprite) and sets record +193. Name from VISUAL_CENSUS. 123 DLLs. |
| 875 | `0x71C0B0` | `FFX_Vfx_QueueSingleSpriteBuild` | 7 / 0 | [V] | One shape version of 0x71B8A0: a kind 1 prim build slot, or for shape type 3 (a2+15) quads filled at step in view space. Called by 0x7110D0, 0x711500 and FFX_Opp_Kind10. 7 DLLs. |

### character access (18 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 6 | `0x82EB60` | `FFX_Ch_SetFlags1Bit2000` | 88 / 0 | [V] | Sets CHR m_flags1 (+0x194) bit 0x2000 to bit 0 of a2, returns the changed bit (0x82EB60). Callers FFX_Oef2_Op151_ChrVisibility, sub_930550. 88 DLLs. |
| 276 | `0x830160` | `FFX_Ch_SetMeshGroupLayer` | 125 / 0 | [V] | CHR +0x187 = a2 and byte +21 of m_instance via sub_63B7D0, the mesh group layer FFX_GfxCtx_LinkMeshGroupLayer compares (0x830176). Callers FFX_Oef2_Op211, sub_930630. 125 DLLs. |
| 263 | `0x8303F0` | `FFX_Ch_GetJointCount` | 5 / 0 | [V] | Returns u16 +10 of m_skeleton (CHR +0x1C0), the joint count (0x8303F0). Callers FFX_Oef2_Op152to153, rcuiPre. 5 DLLs. |
| 398 | `0x830AE0` | `FFX_Ch_SetLightOverrideMask` | 3 / 0 | [V] | CHR +0x188 m_lightOverrideMask = a2 (0x830AE0). Ch 106 SetLightOverrideMask and FFX_MapFx_CommitPointLights call it. 3 DLLs. |
| 354 | `0x8311D0` | `FFX_Ch_ResetTexListA` | 26 / 0 | [V] | sub_831F00(m_texListA, m_texListACount): restores every list A entry's start frame and refreshes it (0x8311D0). Twin of host 355. 26 DLLs. |
| 355 | `0x831300` | `FFX_Ch_ResetTexListB` | 26 / 0 | [V] | sub_831F00(m_texListB, m_texListBCount) then sub_831110, the texture animation refresh (0x831300). Ch 113 ResetTexListB calls it. 26 DLLs. |
| 356 | `0x831390` | `FFX_Ch_SetTexListAFrame` | 49 / 0 | [V] | sub_832050(m_texListA, count, frame a3, index a2): sets one list A entry's frame, index -1 means all (0x831390). Ch 091 and FFX_Ch_Allocate call it. 49 DLLs. |
| 357 | `0x8313C0` | `FFX_Ch_SetTexListBFrame` | 49 / 0 | [V] | sub_832050(m_texListB, count, frame a3, index a2), the list B twin, index -1 means all (0x8313C0). Ch 090 SetTexListBFrame calls it. 49 DLLs. |
| 579 | `0x833940` | `FFX_Ch_SetEffCallback` | 2 / 0 | [V] | CHR +0x7F8 m_effCallback = a2, +0x7FC m_effContext = a3 (0x833940). DLLs 0434, 0610. |
| 236 | `0x833C30` | `FFX_Ch_GetJointWorldMatrix` (was `maybe_FFX_Ch_GetJointWorldMatrix`) | 159 / 0 | [V] | Confirmed. When out and chr->m_inUse: out = CHR world matrix (0x1D0) x joint a2 matrix (+136), translation scaled by 1000 when FFX_Mot_JointIsRemapped (0x833C30). 159 DLLs. |
| 237 | `0x833D10` | `FFX_Ch_GetJointWorldPoint` (was `maybe_FFX_Ch_GetJointMatrix`) | 236 / 0 | [V] | Builds the joint's world matrix like FFX_Ch_GetJointWorldMatrix, then writes a point to a4: offset a3 transformed by it (FFX_Mtx_TransformVec4), or the joint origin when a3 is null (0x833D10). Returns a point, not a matrix. 236 DLLs. |
| 13 | `0x833FC0` | `FFX_Ch_GetScaledWorldMatrix` | 6 / 0 | [V] | Copies CHR +0x210, the scaled world matrix, to a2 when non null and returns its address (0x833FC0). Twin of FFX_Ch_GetWorldMatrix (+0x1D0). 6 DLLs. |
| 410 | `0x8355E0` | `FFX_Ch_GetBone0WorldPos` | 1 / 0 | [V] | FFX_Ch_GetBoneWorldPos(chr, 0, out) (0x8355E0). DLL 0372. |
| 29 | `0x835600` | `FFX_Ch_GetBodyCentre` | 116 / 0 | [V] | Copies CHR +0x694 m_bodyCentre (4 floats) to a2 (0x835600). 116 DLLs. |
| 621 | `0x8356B0` | `FFX_Ch_GetBonePointJointWorldMatrix` | 1 / 0 | [V] | FFX_Ch_LookupBonePoint(chr, point a2) gives a joint, then FFX_Ch_GetJointWorldMatrix(chr, joint, out a4) (0x8356B0). DLL 0367. |
| 705 | `0x8360A0` | `FFX_Map_GetWaterLevel` | 1 / 0 | [V] | Returns g_ffxWaterLevel (0x8360A0), the read side of FFX_Map_SetWaterLevel. Callers FFX_Oef2_Op140, Op161. DLL 0374. |
| 22 | `0x839860` | `FFX_Ch_GetMotionFrame` | 7 / 0 | [V] | Returns CHR +0x740 m_frame, 24.8 fixed point (0x839860). Ch 081 GetMotionFrame and Ch 117 call it. 7 DLLs. |
| 587 | `0x83D070` | `FFX_Ch_SetTexAnimSlotByteE` | 1 / 0 | [V] | When m_texAnimProgram is set, writes byte +0xE of the CHR's texture animation slot (byte_231000E[48 * m_texAnimHandle]) via sub_77FE30, read by FFX_ChTexAnim_Step (0x77F3E3). Ch 141 calls it. DLL 0672. |

### chr render instance (18 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 913 | `0x639170` | `FFX_ChrInstance_PushMeshMatrixOverride` | 4 / 0 | [V] | For each mesh of a render instance, FFX_Gfx_PushModelMatrixOverride(g_ffxGfxCtx, mesh, a2) on the mesh whose id (+24) is a3, or on all meshes when a3 is -1. Exe caller FFX_Opp_Kind18. |
| 920 | `0x63A2B0` | `FFX_ChrInstance_GetBounds` | 0 | [V] | No magic DLL calls this slot. FFX_Ch_GetRenderInstance(a1), then min and max over every mesh bounding box, starting from plus and minus 16777216. |
| 924 | `0x63A410` | `maybe_FFX_ChrInstance_GetBoundsTransformed` | 2 / 0 | [I] | Same min and max walk as slot 920 but each mesh box goes through sub_4F96A0 first, which looks like a transform by the mesh matrix. Called by slot 934. |
| 934 | `0x63A610` | `maybe_FFX_ChrInstance_GetBoundsWithJoints` | 2 / 0 | [I] | Calls slot 924 for the mesh boxes, then walks the per-mesh sub table at +24 to widen a second min and max pair (a6, a7). What the second pair covers is not traced. |
| 847 | `0x63B7F0` | `FFX_ChrInstance_SetWord28` | 14 / 0 | [V] | Stores a2 at +28 of the object a1 when a1 is not null. What +28 means is not traced. |
| 833 | `0x63B810` | `FFX_ChrInstance_SetAlpha` | 14 / 0 | [V] | Writes MaterialColour (1,1,1,a) to every material, picks the fake transparent pass when a<1, unlinks all meshes at a==0 and relinks when it rises again (0x63B810). 14 DLLs. |
| 834 | `0x63B9E0` | `maybe_FFX_ChrInstance_ClearAlphaThreshold` | 2 / 0 | [I] | Walks every mesh of a render instance and writes 0.0 to the 'AlphaThreshold' shader param. |
| 842 | `0x63C430` | `FFX_ChrInstance_SetMeshTransformById` (was `maybe_FFX_ChrInstance_SetBoneMatrix`) | 2 / 0 | [V] | Calls FFX_Gfx_SetMeshTransform(mesh, a2) for every mesh whose id (+24) equals a3. It sets a mesh transform, not a bone matrix. Was maybe_FFX_ChrInstance_SetBoneMatrix. |
| 835 | `0x63C9E0` | `FFX_ChrInstance_SetAddLightColour` | 0 | [V] | No magic DLL calls this slot. Per mesh calls sub_6A9BB0: writes AddLgtClr (r,g,b,a*0.4) and Shininess rgb (strings at 0x6A9BB0). |
| 836 | `0x63CA50` | `FFX_ChrInstance_SetMaterialAndAddLightColour` | 0 | [V] | No magic DLL calls this slot. Per mesh calls sub_6A9CD0: MaterialColour from the first four floats, AddLgtClr and Shininess from the last four. |
| 837 | `0x63CB00` | `FFX_ChrLight_SetLightColour` | 0 | [V] | No magic DLL calls this slot. Sets chr light a1 colour (sub_6AA940, index 3 is the +400 one), refreshes through sub_6AA260 and for index 3 sub_6A7550 (0x63CB00). |
| 930 | `0x63D710` | `FFX_ChrInstance_SetClipPlane` | 2 / 0 | [V] | Writes a vec4 to the 'clipPlane' shader param of every mesh of the instance (sub_635630 walk). |
| 855 | `0x63D810` | `FFX_Chr_ApplyMagicShader` | 4 / 0 | [V] | Per mesh of the chr calls sub_6DFAD0, which clones the materials onto a special shader picked by mode: 0 CharacterBtmToTop, 1 MagicFile249, 2 MagicFile297 (0x6DF3D0, 0x6DF470, 0x6DF510). 4 DLLs. |
| 856 | `0x63D870` | `FFX_Chr_RestoreAllMagicShaders` | 3 / 1 | [V] | sub_6DF620 walks the override map off_C34B4C, restores each mesh's original materials (sub_6DF6D0) and clears both maps (0x63D870). 3 DLLs. |
| 906 | `0x63D8E0` | `FFX_Chr_SetMaterialTexture` | 1 / 0 | [V] | Per mesh calls sub_6DF950, which looks up texture a3 by name (sub_69B4B0) and binds it to material parameter a2 (0x63D8E0). 1 DLL. |
| 857 | `0x63D940` | `FFX_Chr_SetMaterialVec4` | 4 / 0 | [V] | Per mesh calls sub_6DFA30, which writes four floats from four pointers to the material parameter named a2 (0x63D940). 4 DLLs. |
| 914 | `0x641E60` | `FFX_ChrInstance_SetBallParams` | 1 / 0 | [V] | Writes 'Ball_Center' (from a2 with x and y negated, w 1), 'Ball_Range' and 'Ball_Rate' shader params on every mesh of the instance. |
| 818 | `0x642110` | `FFX_ChrInstance_SetVertexColorHeight` | 2 / 0 | [V] | Walks every mesh and writes the 'VertexColorHeightNow' shader param group from the four vectors passed in. |

### effect units and registry (18 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 546 | `0x7FB290` | `FFX_Ch_StartChrEffects` | 85 / 0 | [V] | Finds the CHR in g_ffxChrShadowRegistry and, unless byte_11A0065 or entry bit 1 is set, starts link ids 0x40000400+i .. 0x40000700+i in slots 4..7 of its effect unit via sub_7FC2E0, then sets entry bit 1 (0x7FB290). Core 462 ShowSelfChrEffects calls it. 85 DLLs. |
| 309 | `0x7FBA40` | `FFX_Ch_EffectRegistrySetBit4` | 4 / 0 | [V] | Sets bit 4 of the CHR's effect mask byte (byte_12A85B0 if nonzero else byte_12A84C0), returns 4 if it was clear. Opp kinds test that mask against obj+0xB3 to skip objects (0x811484). Core 463 HideSelfChrEffects calls it. Twin of FFX_Ch_ShadowRegistrySetBit. |
| 310 | `0x7FBAB0` | `FFX_Ch_EffectRegistryClearBit4` | 4 / 0 | [V] | Clears bit 4 of the CHR's effect mask byte (byte_12A85B0 if nonzero else byte_12A84C0), returns 4 if it was clear. Core 462 ShowSelfChrEffects and FFX_Field_ChrRegTask call it (0x7FBAB0). |
| 695 | `0x7FBB90` | `FFX_Ch_KillChrEffects` | 2 / 0 | [V] | Clears registry entry bits 0..1 and frees the CHR's four effect links 0x40000400+i .. 0x40000700+i in effect unit 0 at once via sub_7FDE30 (0x7FBB90). Callers FFX_Ch_ReleaseShadowRegistryEntry, FFX_Oef2_Op082. |
| 525 | `0x7FBCD0` | `FFX_Ch_EndChrEffects` | 86 / 0 | [V] | Clears registry entry bits 0..1, then for each of the CHR's four link records sets 0x4000 in its unit word +176, an end request rather than a free (0x7FBCD0). Caller sub_788E60, Btl 094 ResetChrState. 86 DLLs. |
| 547 | `0x7FC130` | `FFX_Ch_EffectRegistryEndFlagOverride` | 199 / 0 | [V] | For the CHR's slot: byte_12A84C0 = override byte & 0x7F, override byte_12A85B0 = 0, returns the old override (0x7FC167). Undoes FFX_Ch_EffectRegistryBeginFlagOverride. 199 DLLs. |
| 548 | `0x7FC190` | `FFX_Ch_EffectRegistryBeginFlagOverride` | 91 / 0 | [V] | For the CHR's slot: override byte_12A85B0 = normal byte / 0x80, normal byte_12A84C0 = 0, returns the old override (0x7FC1CD). While the override is nonzero the mask setters write it instead. 91 DLLs. |
| 575 | `0x7FC900` | `FFX_Effect_SetScriptSignal2` | 4 / 2 | [V] | byte_11A006E /= 2. FFX_Oef2_Op082 case 20 branches on it (0x814725), op_ovl_effect_free clears the byte. 4 DLLs. |
| 574 | `0x7FC910` | `FFX_Effect_ClearScriptSignal2` | 6 / 0 | [V] | byte_11A006E &= ~2, the clear of the signal FFX_Oef2_Op082 case 20 tests (0x814725). 6 DLLs. |
| 633 | `0x7FC990` | `FFX_Effect_SetScriptSignal1` | 0 / 1 | [V] | byte_11A006E /= 1. FFX_Oef2_Op082 case 19 branches on it (0x81471C). DLL 0393 tail calls it at 0x100024AD. |
| 631 | `0x7FC9A0` | `FFX_Effect_ClearScriptSignal1` | 1 / 0 | [V] | byte_11A006E &= ~1, the clear of the signal FFX_Oef2_Op082 case 19 tests (0x81471C). DLL 0393. |
| 623 | `0x7FC9B0` | `FFX_Ch_AttachChrEffect` | 13 / 0 | [V] | Starts an effect on the CHR's unit: link id = slot + ((counter + 0x300000) << 8) with a per unit counter in byte_119FEF8, sub_7FD7F0(unit, 0, 9, id), returns the id (0x7FC9B0). Callers Btl 131 AttachChrEffect, FFX_Oef2_Op133. 13 DLLs. |
| 624 | `0x7FCA40` | `FFX_Ch_ReleaseChrEffect` | 13 / 0 | [V] | Finds link id a1 in g_ffxEffectLinkRecords and sets 0x8000 in its unit word +176 (0x7FCA40). Pairs with FFX_Ch_AttachChrEffect, called from Btl 132 ReleaseChrEffect through sub_788E30. 13 DLLs. |
| 335 | `0x7FCCC0` | `FFX_Ch_EffectRegistryListChrs` | 1 / 0 | [V] | Copies every nonzero CHR pointer of the 208 registry entries (stride 32 from 0x12A22C0) into the out array and returns the count (0x7FCCC0). DLL 0519. |
| 336 | `0x7FCD10` | `FFX_Ch_EffectRegistryFindChrByKey` | 1 / 0 | [V] | Returns the CHR of the registry entry whose +4 key (the second arg FFX_Ch_Allocate passes to the register call, 0x8252BC) equals a1, else 0 (0x7FCD18). DLL 0519. |
| 717 | `0x7FDC80` | `FFX_Effect_DebugReloadFromHost` (was `maybe_FFX_Effect_StepAll`) | 130 / 0 | [V] | Not a step. Only with g_ffxSgDebugEnabled: for each debug bit in word_2321580 it reloads unit data from a host file with sub_7E5C10 (open, read whole file, gated on byte_2322795) and re-runs FFX_Oef2_SetFromUnit and op_oef2_set (0x7FDC8D). 130 DLLs call it. |
| 568 | `0x7FF090` | `maybe_FFX_Effect_SetResidentEffectsEnabled` | 4 / 0 | [I] | Stores a1 in byte word_11A0063, maybe_g_ffxTexSkipResident = !a1, and clears (a1 != 0) or sets (a1 == 0) mask bit 2 in byte_12A84C0 for every registered CHR (0x7FF0CE). FFX_Rand_SeedCosmeticStreams does the off case at init. Callers FFX_Oef2_Op041, sub_A54760. |
| 722 | `0x7FF2B0` | `FFX_Effect_DebugSetMeterSlot` | 130 / 0 | [V] | Debug only (g_ffxSgDebugEnabled): resets record a2 & 3 of dword_22C9760 (sub_A649C0), then stores {a2, a1} there when a2's 0xF00 priority allows (sub_A64B60). Callers FFX_MapFx_LoadParticleSet, FFX_MapFx_RegisterHeapMeter. Nothing on PC reads it. 130 DLLs. |

### gfx misc (13 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 846 | `0x639350` | `FFX_Gfx_ChrClusterToMainWorld` | 2 / 3 | [V] | Calls 0x6578E0: if gfx+0xFB44 is clear, moves the chr cluster gfx+0x11E4 from world gfx+0x9C to gfx+0x7C and sets the flag. Undoes FFX_GfxCtx_PushLayerBreak. 2 DLLs. |
| 845 | `0x63DF50` | `FFX_Gfx_ChrClusterToLayerWorld` | 4 / 2 | [V] | Tail jump to 0x65A550: if gfx+0xFB44 is set, moves the chr cluster gfx+0x11E4 from world gfx+0x7C to gfx+0x9C and clears the flag. Mirror of 0x639350. 4 DLLs. |
| 878 | `0x641E40` | `FFX_Gfx_SetSceneClearMode` | 3 / 0 | [V] | Writes gfx+3 (sub_66D1B0) and byte_CCB460. gfx+3 == 1 makes FFX_Frame_RenderScene begin the scene with clear 0xA (0x6699BB), byte_CCB460 == 1 drops layer 1 VFX on some maps in FFX_Vfx_SubmitModel. 3 DLLs. |
| 863 | `0x6429F0` | `FFX_MagicApi_RenderDelegationAdd` | 0 | [V] | No magic DLL calls this slot. One line forwarder to RenderDelegationHelper_Add(Src, Size, a3, a4). |
| 858 | `0x642FE0` | `FFX_Gfx_SetOrthoCapture` | 9 / 0 | [V] | Fills gfx+0xFC00..0xFC68 through sub_66D000: enable, matrix, two vec4, and a texture id printed as "%d". sub_66B560 renders the scene with an orthographic camera from these, FFX_MagicTex_ResolveByName matches the id. 9 DLLs. |
| 801 | `0x643630` | `FFX_Gfx_MulMapColorScale` | 121 / 0 | [V] | Calls sub_66D3B0(gfx, r, g, b, a4, a5, a6, 0), which multiplies the gfx+64632 colour scale and writes "clrScaleFactor" to the map materials. 121 DLLs. |
| 933 | `0x643670` | `FFX_Gfx_SetMapColorScale` | 225 / 0 | [V] | Calls sub_66D1D0(gfx, r, g, b, a4), which sets gfx+64632 = (r,g,b,1) and writes "clrScaleFactor". Callers include FFX_Oef2_Op144 and MagicFile__stop. 225 DLLs. |
| 841 | `0x6436A0` | `FFX_Gfx_MulMapColorScaleAlt` | 0 | [V] | No magic DLL calls this slot. Same as 0x643630 with the last sub_66D3B0 flag 1. |
| 918 | `0x6437E0` | `maybe_FFX_Vfx_SetDrawMdl2Param` | 1 / 0 | [I] | Writes dword_CEC1A0, whose only reader is sub_641370 called from pppDrawMdl2_Draw. 1 DLL. |
| 859 | `0x6438C0` | `FFX_Gfx_SetOrthoCaptureTexId` | 4 / 0 | [V] | sub_66E5E0: when gfx+0xFC00 is enabled, sets the capture id +0xFC64 and its "%d" string +0xFC68 (see 0x642FE0). 4 DLLs. |
| 848 | `0x643B80` | `FFX_HostApi_UnusedSlot848` | 0 | [V] | No magic DLL calls this slot. Reads a vector at +112 of the object at g_ffxGfxCtx+220 through sub_573D00 and sub_4F93F0. |
| 864 | `0x645E60` | `FFX_MagicApi_QueueScreenFadeClear` | 0 | [V] | No magic DLL calls this slot. Queues FFX_ScreenFade_ClearCallback on the render delegation list dword_CCCA98 at priority a1 (2049 when a1 is -1). Exe caller FFX_Opp_Kind04. |
| 876 | `0x699080` | `FFX_HostApi_UnusedSlot876` | 0 | [V] | No magic DLL calls this slot. Sets byte +74692 of the object at dword_CCC8A0 to 1. |

### sound (12 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 528 | `0x79FD20` | `FFX_Btl_SetUnitSoundKind6Flag` | 3 / 0 | [V] | Writes BTLUNIT +0x40F. sub_79FE80, the unit sound handle picker, refuses sound kind 6 while it is 0. Called three times by FFX_Oef2_Op043 (a sound op). 3 DLLs call it. |
| 593 | `0x7B2B70` | `FFX_Btl_PrepareVoice` | 2 / 0 | [V] | If a voice id is pending (dword_112C8F0) returns -1 while the loader or streamer is busy (sub_7B2C10(1)), else FFX_Voice_Prepare(id). Returns 0 when done. 2 DLLs call it (0052, 0053). |
| 595 | `0x7B2BA0` | `FFX_Btl_StartVoice` | 2 / 0 | [V] | If a voice id is pending returns -1 while busy (sub_7B2C10(0)), else FFX_Voice_StartDefault and shows the pending system message dword_112C8F4 through sub_79D090. Returns 0 when done. 2 DLLs call it. |
| 594 | `0x7B2BE0` | `FFX_Btl_IsVoiceBusy` | 2 / 0 | [V] | 1 while FFX_Voice_IsBusy or the battle message window 7 is still up (sub_79D050), else 0. Btl 103 IsVoiceBusy and 104 WaitVoiceDone. 2 DLLs call it. |
| 408 | `0x7FFC60` | `FFX_EffectSnd_UpdateAtPos` | 341 / 0 | [V] | For a kind 6 sound entry: pan and volume from a 3D position (sub_7FFA90) or centre pan 64 and 127*vol/256. Type 1 SE still playing gets FFX_Snd_SetSePanById and SetSeVolumeById, type 2 voice gets FFX_Voice_SetVolume and pan (0x7FFC60). FFX_Oef2_Op043. 341 DLLs. |
| 393 | `0x7FFD20` | `FFX_EffectSnd_PlayAtPos` | 349 / 0 | [V] | Starts a kind 6 sound entry with pan and volume from a 3D position (sub_7FFA90) or centred. Type 1 starts the SE by id (sub_886E50), type 2 stops the voice and starts battle streaming on the matching channel (0x7FFD20). FFX_Oef2_Op043. 349 DLLs. |
| 663 | `0x7FFDE0` | `FFX_EffectSnd_FadeOut` | 10 / 0 | [V] | Type 1 sound entry: FFX_Snd_FadeSeVolumeById(se id, a2, 0), fading the SE to zero (0x7FFDE0). FFX_Oef2_Op043. 10 DLLs. |
| 220 | `0x7FFE10` | `FFX_EffectSnd_Play` | 358 / 0 | [V] | Starts a kind 6 sound entry with explicit pan a2 and volume 127*a3/256. Type 1 starts the SE by id (sub_886E50), type 2 stops the voice and starts battle streaming (0x7FFE10). FFX_Oef2_Op043 calls it with pan 64, vol 256. 358 DLLs. |
| 549 | `0x8000F0` | `FFX_SpuStream_PollRequest` (was `maybe_FFX_SpuStream_PollRequest`) | 216 / 0 | [V] | Confirmed. Returns 0x2000/0x1000 while stream request 0 is pending or finishing, 0x2001/0x1001 for request 1, else the queued SE load count HIWORD(dword_23C3640), 0 when idle (0x8000F0). FFX_Oef2_Op043. 216 DLLs. |
| 221 | `0x8002C0` | `FFX_SpuStream_QueueSeLoadRange` | 369 / 0 | [V] | Unless magic 507, for kind 6 entries a2..a3 of the unit queues each type 1 SE into the 32 entry load ring with owner mask a4 (sub_7FFEB0 -> FFX_Se_LoadSeSepProjects via FFX_Snd_LoadSeSepChunk). Returns 0 (0x8002C0). Callers FFX_Oef2_Op043, FFX_Yanagi_InitProgram. 369 DLLs. |
| 550 | `0x800330` | `FFX_SpuStream_DrainRequests` | 100 / 0 | [V] | Loops maybe_FFX_SpuStream_ServiceRequest until both stream requests and the queued SE loads are done (0x800330). The blocking form of the poll. 100 DLLs. |
| 551 | `0x800390` | `FFX_SpuStream_RequestStream` | 132 / 0 | [V] | For a type 2 (stream) sound entry, claims stream request slot a2 & 1 if free and returns 0, else -1 (0x800390). FFX_Oef2_Op043. 132 DLLs. |

### texture and GS (12 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 378 | `0x7E4C30` | `FFX_Tex_FlushPendingImageLoads` | 84 / 0 | [V] | Walks the 64 queued image entries at dword_11A01A0, issues the GS LoadImage stubs (split in 256 row strips for big images), then clears the three queue arrays and the count dword_11A04A0. On PC the GS calls are stubs. 84 DLLs call it. |
| 379 | `0x7E4E40` | `FFX_Tex_ReleaseGroup` | 3 / 0 | [V] | Clears every entry of the 64 group masks at dword_11A04C0 that shares a bit with a1 and trims the live count word_11A04B0 to the last used entry. Callers op_fp_effect_free, op_ovl_effect_free and others. 3 DLLs call it. |
| 380 | `0x7E4E80` | `FFX_Tex_ReloadGroup` (was `maybe_op_oef2_clut_load_pal`) | 4 / 0 | [V] | Was maybe_op_oef2_clut_load_pal. For each live entry whose group mask (dword_11A04C0) shares a bit with a1, calls op_oef2_texture_load(dword_11A0600[i], 1) unless byte_11A05C0[i] is set. The CLUTINFO string is only the null check log. 4 DLLs call it. |
| 348 | `0x807870` | `FFX_Oef2_ApplyChrClutHsv` | 9 / 0 | [V] | Writes recoloured CLUTs into the CHR's texture list A copy: per entry sub_807B70 shifts the saved HSV by hue a3, sat a4, value a5 and blend a6 (hue wraps at 768), and the shade coeff at CHR +0x36C when a4 + a5 is 0. Skipped when sub_7FCC80 sees registry byte +0x17 bit 0 (0x807870). FFX_Oef2_Op040. 9 DLLs. |
| 349 | `0x8079F0` | `FFX_Oef2_SaveChrClutHsv` | 9 / 0 | [V] | Converts every CLUT colour of the CHR's texture list A (original data, sub_83C9B0) to HSV words with sub_807E90 into buffer a2 (count, per entry counts, data from +32), then sets render instance mode 1 (0x8079F0). FFX_Oef2_Op040. 9 DLLs. |
| 350 | `0x807B20` | `FFX_Oef2_GetChrClutHsvSize` | 5 / 0 | [V] | Size of the HSV buffer FFX_Oef2_SaveChrClutHsv fills: 8 bytes per CLUT colour over all list A copy entries, 16 aligned, plus 32, or 0 (0x807B20). 5 DLLs. |
| 245 | `0x820530` | `FFX_Gs_GetDBuff` | 39 / 0 | [V] | Writes g_ffxMainStepCounter to *a1 when a1 is non null and returns &g_ffxGsDBuff, never null (0x820530). 16 exe callers incl. FFX_Vfx_StepAll and the ppp draw ops. 39 DLLs. |
| 562 | `0x83B650` | `maybe_FFX_Gs_GetPacketBuffer` | 38 / 0 | [I] | Returns off_C498DC, which holds 0x1F00000, a static packet work buffer (0x83B650). Menu and debug draws take it before building prims, and the pppDrawMdl ops read the pointer directly (0x7379C7). 38 DLLs. |
| 345 | `0x83C710` | `FFX_Ch_CreateTexListACopy` | 9 / 0 | [V] | Frees any old copy, allocates m_texListACopy (CHR +0x704) sized for every list A CLUT, names it '%s:CLUT', fills it (sub_83CA20) and switches the CHR to it (sub_83C940). Log text 'status_stone=%d(Create)'. Counterpart of FFX_Ch_ReleaseTexListACopy (0x83C710). 9 DLLs. |
| 584 | `0x83C9D0` | `FFX_Ch_GetTexListACopyData` | 1 / 0 | [V] | Returns dword +0 of entry a2 in m_texListACopy (CHR +0x704), the private CLUT data that FFX_Oef2_ApplyChrClutHsv writes, or 0 with no copy (0x83C9D0). DLL 0672. |
| 765 | `0xC3E6C0` | `g_ffxClutSwizzleTable` | copied by 581 | [V] | 32 byte PS2 CLUT index swizzle, 0..7, 16..23, 8..15, 24..31. pppKeTkFade_Draw 0x72F015 maps a palette index through it. |
| 763 | `0x11A0710` | `g_ffxGsDrawEnvPacket` | copied by 581 | [V] | GS draw environment packet buffer. FFX_Gs_BuildDrawEnvPacket 0x7EB760 fills +0x10.. (GIF tag 0x8001, A+D) and FFX_Gs_SendDrawEnvPacket 0x7EB580 copies it out. |

### magic draw and map (9 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 556 | `0x9088D0` | `FFX_MagicDraw_GetColorScale` (was `maybe_FFX_MagicDraw_GetVec4`) | 224 / 0 | [V] | Copies the vec4 at flt_18DF00C out (0x9088D0). sub_908980 multiplies the fog colour bytes by this vec4 and adds the bias at flt_18DF01C, so it is the colour scale, not a generic vec4. Was a maybe_ guess. |
| 557 | `0x908960` | `FFX_Map_GetPartMask` | 8 / 0 | [V] | Returns dword_18DED0C, the map part mask that MapFx 000 (sub_88EBA0) edits one bit at a time and stores back through host 560. Also read by Oef2 op 144. ATEL_MISC_LIBRARIES.md row 0. |
| 558 | `0x908980` | `FFX_MagicDraw_GetFogColorScaled` | 79 / 0 | [V] | Writes the fog colour (dword_18DF140 bytes) to out, and when g_ffxMagicDrawFlags bit 2 is set scales each channel by flt_18DF00C and adds flt_18DF01C, clamped to 0..255. Its exe callers are the AtelSys MapFx 063..068 fog colour get and set syscalls. |
| 559 | `0x908BB0` | `FFX_Map_GetFogRange` | 5 / 0 | [V] | (near, far, limit) out. Near and far come from flt_18DF148 and flt_18DF14C scaled by flt_18DEFF8, limit from flt_18DF144, and event 224 overrides all three. Callers are the AtelSys MapFx 056..061 fog near, far and limit syscalls. |
| 406 | `0x908DF0` | `FFX_Map_GetPartMatrix` | 1 / 0 | [V] | (index, out): copies the 64 byte matrix of map part index from the 180 byte records at unk_18DF59C, returns -1 when index >= dword_18DF588 (the part count MapFx 101 checks). Write side is sub_90A520. Yanagi tasks bound to a map part use it (0x92FC60). |
| 397 | `0x909FF0` | `FFX_MagicDraw_SetColorBias` (was `maybe_FFX_MagicDraw_SetColorBias`) | 230 / 0 | [V] | Stores four ints minus 128 as floats in flt_18DF01C..flt_18DF028, the additive half of the colour scale and bias that sub_908980 applies. Confirms the guess. |
| 332 | `0x90A040` | `FFX_MagicDraw_SetClearColor` (was `maybe_FFX_MagicDraw_SetClearColor`) | 345 / 0 | [V] | Stores RGBA bytes in byte_18DF178 and, unless magic 706 is loaded, sets the app clear colour through sub_642E90 (Phyre application +128..+136). Called by AtelSys MapFx_069_SetClearColor. Confirms the guess. |
| 353 | `0x90A270` | `FFX_MagicDraw_SetColorScale` (was `maybe_FFX_MagicDraw_SetVec4`) | 347 / 0 | [V] | Copies a vec4 into flt_18DF00C, the colour scale sub_908980 multiplies by. Pair of FFX_MagicDraw_GetColorScale. |
| 560 | `0x90A2B0` | `FFX_Map_SetPartMask` | 8 / 0 | [V] | dword_18DED0C = a1. Write side of host 557, also called by the map region load task, Oef2 op 144 and MapFx 000 (sub_88EBA0). |

### lights (8 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 400 | `0x831950` | `FFX_Light_AllocPointLights` | 3 / 0 | [V] | Frees the old set, then FFX_MemAlloc(44 * count) into dword_13008CC with count in dword_13008D0 and default fields (0x831950). Sg 035 AllocPointLights calls it. 3 DLLs. |
| 401 | `0x8319B0` | `FFX_Light_SetPoint` | 3 / 0 | [V] | Fills point light record i: position, radius, rgb, group, falloff 2.0, enabled 1 through sub_831CA0..sub_831C60 (0x8319B0). Sg 037 SetPointLight and the pppFpPointLight ops call it. 3 DLLs. |
| 402 | `0x831A30` | `FFX_Light_FreePointLights` | 3 / 0 | [V] | Calls sub_831BA0: FFX_MemFree(dword_13008CC), zero the pointer and count, FFX_Ch_RequestMemoryMove (0x831BA0). Sg 036 FreePointLights calls it. 3 DLLs. |
| 544 | `0x8360F0` | `FFX_Light_GetColor` | 30 / 0 | [V] | Copies light colour i (0..3, 0xC49744 + 16*i) to a2 (0x8360F0). The read side of FFX_Light_SetColor. 30 DLLs. |
| 588 | `0x8361C0` | `FFX_Light_GetDir` | 8 / 0 | [V] | Copies light direction i (0..2: 0xC49724, 0xC49734, 0x13008E8) to a2 (0x8361C0). The read side of FFX_Light_SetDir. 8 DLLs. |
| 545 | `0x836240` | `FFX_Light_GetDirMatrix` | 18 / 0 | [V] | Returns &unk_1300978, the matrix FFX_Light_SetDir builds from the three directions (sub_6ED620 in 0x836430). 18 DLLs. |
| 589 | `0x836310` | `FFX_Light_SetColor` | 6 / 0 | [V] | Stores light colour i (0..3), rebuilds the colour matrix dword_23C4400 and pushes it via sub_63CB00 (0x836310). sub_836520, the Sg 019 SetLight path, passes rgb/255. 6 DLLs. |
| 590 | `0x836430` | `FFX_Light_SetDir` | 6 / 0 | [V] | Stores light direction i (0..2), rebuilds the direction matrix unk_1300978 and pushes it to FFX_ChrLight via sub_63CB50 (0x836430). sub_836520 builds the vector from yaw and pitch degrees. 6 DLLs. |

### camera (7 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 287 | `0x7877D0` | `FFX_Btl_GetCameraHandle` | 43 / 0 | [V] | Returns the battle camera handle for class a2 (0..7), opening it with FFX_Came_OpenHandle on first use and caching it in dword_112C22C[a2]. FFX_Btl_CloseAllCameraHandles closes the same array. Called by FFX_Oef2_Op057_Camera. 43 DLLs call it. |
| 281 | `0x79DDB0` | `FFX_BtlSpecial_SetSuspended` | 43 / 0 | [V] | Writes the high byte of word_112A8E6, saving the old value once (byte_112A8E8, byte_112A8E9) for sub_79DD90 to restore. While non zero sub_79DD70 returns 0 so FFX_Came_StepAll skips the BtlSpecial camera (sub_79DFE0). Debug label "ps". Callers FFX_Oef2_Op057_Camera. 43 DLLs call it. |
| 690 | `0x7BC750` | `FFX_Came_SetScreenDrwRect` | 2 / 0 | [V] | Screen a1 (0..2): sets the DRW target (+0x90) and the live rect (+0x50) to (x, y, w, h). Came 080 SetScreenDrwRect. 2 DLLs call it. |
| 916 | `0x7BD860` | `FFX_Came_StartTrapezoidMove` | 1 / 0 | [V] | For the active camera handle: arms a trapezoid tween (type 2, accel, hold, decel, start divisor) on each axis the record +7 mask does not lock, on the REF or CAM sub-block by a2 & 1. Came 020 and 021 CamMoveTrapezoid. 1 DLL (0475). |
| 283 | `0x7BED60` | `FFX_Came_StopMove` | 43 / 0 | [V] | For the active camera handle: zeroes the tween counters and sets the pending position to the committed one (FFX_Came_SetPos), and for the eye re-applies the roll and ScrDpt. Came 022 CamStopMove and 052 RefStopMove. 43 DLLs call it. |
| 285 | `0x7BFA80` | `FFX_Came_SetRoll` | 43 / 0 | [V] | Sets the roll target of the camera handle record and slot +0xC94, snapped into +0xC90 when no roll tween runs. Pairs with FFX_Came_GetRoll. Came 058 SetRoll. 43 DLLs call it. |
| 286 | `0x7BFBD0` | `FFX_Came_SetScrDpt` | 44 / 0 | [V] | Sets the ScrDpt (projection distance) target, slot +0xCA0, snapped into +0xC9C when no tween runs. Pairs with FFX_Came_GetScrDpt, MsCameraSetScrDpt wraps it. 44 DLLs call it. |

### ppp core (7 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 759 | `0xC498DC` | `g_ffxSharedWorkBufPtr` | copied by 581 | [V] | Holds 0x1F00000, a large work buffer also used by movie_str, Oef2 op 84 and the menus. The pppDrawMdl draws store it into their packet (pppDrawMdlBS_Draw 0x7379C7). |
| 761 | `0x230FD30` | `g_ffxPppDrawHookTable` | copied by 581 | [V] | Table of draw hook callbacks. pppDrawHook_Draw 0x757398 calls entry [op byte +14] with (part, arg), and FFX_Vfx_FreePrimList zeroes it (0x711F93). No exe code fills it, so a DLL installs its own table through this slot. |
| 762 | `0x230FD50` | `g_ffxPppOwnerScale` | copied by 581 | [V] | vec4 scale of the effect's owner. opu_part_run 0x80B86D writes unit +0x3C times a constant, the rcbg path at 0x92F0CC writes per axis scales, and pppChrSclXYZMatrix_Calc 0x73492D reads it. |
| 755 | `0x230FE20` | `g_ffxPppDrawMatrix` | copied by 581 | [V] | The ppp draw matrix. FFX_MapFx_ApplyViewMode 0x72A260 copies the model view result into it, and pppDrawMatrix_Draw 0x734B70 multiplies a particle's local matrix by it. |
| 757 | `0x230FF20` | `g_ffxPppModelViewMatrix` | copied by 581 | [V] | View x part world matrix (or the camera aligned or shared basis variant), built by FFX_MapFx_ApplyViewMode 0x72A260 and read by the pppKeDMat and pppKeZCrct draws. |
| 754 | `0x230FFA0` | `g_ffxPppPartWorldMatrix` | copied by 581 | [V] | The current particle's world matrix. sub_717410 builds it from the part's rotation (+80), scale (+96..+104) and translation (+64..+72), FFX_MapFx_ApplyViewMode 0x72A260 multiplies it by the view. |
| 741 | `0x230FFE0` | `g_ffxPppDefaultHeap` | copied by 581 | [V] | The dword the ppp heap pointer g_ffxPppCurrentHeap (0xC3A4CC) points at from load, its initial value is 0x230FFE0. FFX_Ppp_StartPart 0x7123C0 and FFX_Vfx_FreePrimList 0x711F20 allocate and free particle storage through *it. No exe code names it directly. |

### ppp operators (7 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 83 | `0x736570` | `pppKeTh_Des` | 0 / 113 | [V] | Empty Des (destructor) column of ppp operator pppKeTh, record 61 of the map and DLL op tables (refs 0xC3AEB4, 0xC3C55C) and PM row 147. 113 DLLs reach it only through their pppProg thunks. |
| 571 | `0x7372B0` | `pppNeiChrPointLight_Con` | 0 / 3 | [V] | Empty Con column of ppp operator pppNeiChrPointLight, DLL op table row 211 (ref 0xC3DCC4) and PM row 148. Reached through pppProg thunks in 3 DLLs. |
| 572 | `0x7372C0` | `pppNeiChrPointLight_Des` | 0 / 3 | [V] | Empty Des column of ppp operator pppNeiChrPointLight, DLL op table row 211 (ref 0xC3DCCC) and PM row 148. Reached through pppProg thunks in 3 DLLs. |
| 126 | `0x755000` | `pppKeShpDtt_Calc` | 0 / 188 | [V] | Empty Calc column of ppp operator pppKeShpDtt, row 45 of the map and DLL op tables (refs 0xC3AC18, 0xC3C2C0) and PM row 197. Sits right before pppKeShpDtt_Con. 188 DLLs reach it through pppProg thunks. |
| 107 | `0x7597A0` | `pppKeGrvEff_Con` | 0 / 109 | [V] | Empty Con column of ppp operator pppKeGrvEff, row 93 of the map and DLL op tables (refs 0xC3B3AC, 0xC3CA54) and PM row 46. 109 DLLs reach it through pppProg thunks. |
| 167 | `0x759D70` | `pppKeHmgEff_Con` | 0 / 55 | [V] | Empty Con column of ppp operator pppKeHmgEff, DLL op table row 170 (ref 0xC3D65C) and PM row 45. 55 DLLs reach it through pppProg thunks. |
| 422 | `0x75B680` | `pppKeAcmSolid_PostPass` | 0 / 4 | [V] | Empty PostPass column (+24) of ppp operator pppKeAcmSolid, DLL op table row 251 (ref 0xC3E300) and PM row 239. YONISHI_ENGINE.md says only pppKeAcmSolid has a PostPass, and this is it. 4 DLLs reach it through thunks. |

### camera and screen (6 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 693 | `0x82BD10` | `FFX_Sg_SetAccBufAlpha` | 2 / 0 | [V] | Accumulation buffer alpha: target level a1 tweened over a2 frames (step word_1300712), instant when a2 is 0, event 258 special case (0x82BD10). Sg 026 SetAccBufAlpha calls it. 2 DLLs. |
| 694 | `0x82BDC0` | `FFX_Sg_SetOffscreenClear` | 3 / 0 | [V] | Stores mode and rgb bytes at byte_1300771..774, read by FFX_Gs_BeginFrameDrawEnv (0x82BDC0). Sg 057 SetOffscreenClear calls it. 3 DLLs. |
| 543 | `0x82BEE0` | `FFX_Sg_SetMotionBlur` | 224 / 0 | [V] | Motion blur: strength a1 enables it and sets level a1/128 (sub_643730, sub_643750), 0 disables (0x82BEE0). Sg 010 SetMotionBlur, came 125 via sub_7BCDD0, FFX_Btl_BeginBattle. 224 DLLs. |
| 583 | `0x82CBA0` | `FFX_ScreenFade_RequestMode4` | 1 / 0 | [V] | FFX_ScreenFade_Request(frames, 1, 1), which stores fade mode 4. Fills the gap between RequestMode2 and RequestMode3 (0x82CBA0). Caller FFX_Field_StepRandomEncounter. DLL 0671. |
| 391 | `0x82CC20` | `FFX_Sg_SetScreenTint` | 5 / 0 | [V] | Additive screen tint, fade channel type 7: rgb and level a4, level 0 clears (0x82CC20). Sg 008 SetScreenTint calls it. 5 DLLs. |
| 420 | `0x82CCA0` | `FFX_Sg_StartScreenColorFade` | 1 / 0 | [V] | Screen fade channel type 6 toward rgb over a1 frames (0x82CCA0). Sg 003 ScreenColorFade and SG_DebugWin_FlashProc call it. DLL 0661. |

### file and loader (6 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 923 | `0x9DAFD0` | `MagicFile__setTexRemapFlag` | 1 / 0 | [V] | dword_193FDAC = a1. MagicFile__load and unload clear it, sub_9DA420 reads it, and FFX_MagicTex_ResolveByTex0 remaps TBP 0x3600 to 0x3601 when it is 1 and magic 539 is loaded (0x660510). Only DLL 0539 calls it. |
| 917 | `0x9DB170` | `FFX_MagicApi_GetCurrentEventId` | 1 / 0 | [V] | Returns g_ffxCurrentEventId. |
| 771 | `0x9DB230` | `FFX_MagicApi_MakeOpHandle64` | 8 / 0 | [V] | Returns (a1 << 32) + g_ffxPppCurrentOpIndex, a 64 bit handle of the current ppp operator. |
| 772 | `0x9DB250` | `FFX_MagicApi_MakeHandle64` (was `maybe_FFX_MakeHandle64`) | 123 / 0 | [V] | Returns (a1 << 32) + a2. Confirms the guess. 123 DLLs. |
| 769 | `0xC64CE0` | `maybe_g_ffxMagicHostApiVersion` | copied by 581 | [I] | The dword right before the table, value 1, then a zero. Nothing in FFX.exe references it except the table entry. A version or ABI marker is a guess. |
| 935 | `0x1940AC4` | `g_ffxMagicHideChrOnStop` | copied by 581 | [V] | MagicFile__stop 0x9DA939 sets it to 1 when the stopped magic id is 344, 437 or 438. FFX_Ch_StepAll 0x82F403 then hides the first chr whose model is 4225, 4195, 4196, 4372 or 4235 and clears it. |

### ppp runtime (6 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 713 | `0x711F20` | `FFX_Vfx_FreePrimList` (was `maybe_FFX_Vfx_FreePrimList`) | 532 / 116 | [V] | Frees every particle on the list at +48 (FFX_Ppp_FreeParticle), clears the head, frees the block at +60 from the current ppp heap, and clears g_ffxPppDrawHookTable (slot 761). Body matches the guess. Was a maybe_ guess. |
| 776 | `0x712C30` | `FFX_Vfx_AlphaFuncToBlend` (was `maybe_FFX_Vfx_AlphaFuncToBlend`) | 249 / 0 | [V] | Maps the PS2 ALPHA register byte to blend flags: 0x41 -> 256, 0x42 -> 16, 0x44 -> 0, 0x46 -> 32, 0x48 -> 2, 0x88 -> 512, anything else logs and returns 0. Was a maybe_ guess. |
| 160 | `0x716710` | `FFX_Vfx_ForEachPrim` (was `maybe_FFX_Vfx_ForEachPrim`) | 339 / 0 | [V] | Walks the particle list at +48 and calls a2[type] (type is byte +14 of the particle record) with (particle, a3). Body matches the guess. Was a maybe_ guess. |
| 351 | `0x716A30` | `FFX_pppMemAlloc` (was `maybe_FFX_pppMemAlloc`) | 4 / 0 | [V] | FFX_PppHeap_Alloc from heap a1, falling back to heap a2 when it is live, and sets g_ffxPppAllocFailed on failure. Body matches the guess. Was a maybe_ guess. |
| 258 | `0x717410` | `FFX_Ppp_BuildPartWorldMatrix` | 62 / 0 | [V] | Builds g_ffxPppPartWorldMatrix (slot 754, 0x230FFA0) from the part rotation at a1+80 (FFX_Ppp_RotMatrixXYZ), scaled per row by a1+96..+104, plus the translation. Callers include opu_part_run and FFX_Vfx_StepAll. |
| 259 | `0x7175F0` | `FFX_Ppp_EmptyHook` | 4 / 0 | [V] | Empty function next to FFX_Ppp_CalcPassChecked. Four DLLs call it. |

### PRX pointer (5 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 941 | `0xA44650` | `FFX_PrxPtr_Get` (was `maybe_FFX_PrxPtr_Get`) | 539 / 0 | [V] | Returns its argument. A PS2 PRX relative pointer resolve that is the identity on PC. 539 DLLs, 961 exe xrefs. Confirms the guess. |
| 940 | `0xA44660` | `FFX_PrxPtr_SetLast` (was `maybe_FFX_PrxPtr_SetLast`) | 0 | [V] | No magic DLL calls this slot. Stores its argument in dword_1A85F38. Confirms the guess. |
| 944 | `0xA446A0` | `FFX_PrxPtr_IsNull` (was `maybe_FFX_PrxPtr_IsNull`) | 0 | [V] | No magic DLL calls this slot. return a1 == 0. Confirms the guess. |
| 943 | `0xA446B0` | `FFX_PrxPtr_Add` (was `maybe_FFX_PrxPtr_Add`) | 0 | [V] | No magic DLL calls this slot. return a1 + a2. Confirms the guess. |
| 942 | `0xA44700` | `FFX_PrxPtr_Set` (was `maybe_FFX_PrxPtr_Set`) | 35 / 0 | [V] | Returns its argument, the store side of FFX_PrxPtr_Get. Confirms the guess. |

### magic draw (5 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 811 | `0x906680` | `FFX_Magic_SetSpriteVertexColor` (was `maybe_FFX_Magic_SetTexParam`) | 10 / 0 | [V] | Stores rgba ints at +16 of sprite corner a5 in the 32 byte records at 0x25D1700 that FFX_Magic_DrawSpriteTex0 hands to FFX_Sprite2D_Draw (0x906481). Not a texture parameter. 10 DLLs, 280 sites. |
| 810 | `0x9066B0` | `FFX_Magic_SetSpriteVertexUv` | 10 / 0 | [V] | Stores u, v at +8 of sprite corner a3 (flt_25D1708), the uv pair FFX_Magic_DrawSpriteTex0 clips (0x9064CC). 10 DLLs. |
| 809 | `0x9066D0` | `FFX_Magic_SetSpriteVertexPos` | 10 / 0 | [V] | Stores x, y at +0 of sprite corner a3 (flt_25D1700), the position pair FFX_Magic_DrawSpriteTex0 clips (0x9064A0). 10 DLLs, 280 sites. |
| 555 | `0x908660` | `FFX_MagicDraw_GetColorBias` | 224 / 0 | [V] | Returns the colour bias flt_18DF01C..028 plus 128 as four ints (0x908663), the read side of FFX_MagicDraw_SetColorBias, which stores value - 128. 224 DLLs. |
| 331 | `0x9086D0` | `FFX_MagicDraw_GetClearColor` | 303 / 0 | [V] | Returns clear colour bytes byte_18DF178..17B as ints, or with g_ffxMagicDrawFlags 1 and 4 set, colour * scale flt_18DF00C + bias flt_18DF01C clamped 0..255 (0x90874A). MapFx 073..075 and FFX_Gs_BeginFrameDrawEnv read it. 303 DLLs. |

### ATEL helpers (4 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 413 | `0x86AE90` | `FFX_Atel_GetPartyCharChr` (was `maybe_FFX_Atel_PartyCharHasPendingScript`) | 337 / 0 | [V] | Finds the actor of party character a1 and returns its CHR (+0x9C) when attach kind +0xAA is 1, else 0 (0x86AE90). Not a pending script test. MapFx 088 and Save 053 use the result as a CHR. 337 DLLs. |
| 260 | `0x86AEE0` | `FFX_Atel_GetPartyCharChr_dup` | 32 / 0 | [V] | Same body as FFX_Atel_GetPartyCharChr 0x86AE90, only the call displacements differ: returns party character a1's CHR when its actor is CHR backed (0x86AEE0). FFX_Oef2_Op100. 32 DLLs. |
| 261 | `0x86B030` | `FFX_Atel_TestSignalFlag` (was `maybe_FFX_Atel_TestFlag`) | 45 / 0 | [V] | Returns bit a1 of dword_1326B70, the per map signal mask Core 378/379 set and clear (0x86B03D). Core 376 and 377 WaitSignalFlag call it. 45 DLLs. |
| 306 | `0x875FB0` | `FFX_Atel_ShowTextOverlay` | 111 / 0 | [V] | Text overlay slot a1 (clamped 0..7) loads entry a2 (<= 63) of table a3 into both layer records via sub_876180 and sets the state byte to 1 (0x875FB0). Core 491 and 492 call it. 111 DLLs. |

### blur effect (4 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 821 | `0x639110` | `FFX_MagicApi_BlurExRequest` | 54 / 0 | [V] | Gets the BlurEffectEx singleton (sub_6C4A70, prints 'BlurEffectEx: singleton is not created yet.' when missing) and stores a1 at +44 with the request byte +40 set to 1 (sub_6C4A90). Exe caller FFX_Opp_Kind07. |
| 822 | `0x643830` | `FFX_MagicApi_BlurExSetPositiveParams` | 38 / 0 | [V] | BlurEffectEx floats +92..+104 = a1..a4, each written only when > 0 (sub_6C4620). Exe caller FFX_Opp_Kind07. What the four floats control is not traced. |
| 824 | `0x643860` | `FFX_MagicApi_BlurExSetVec4` | 38 / 0 | [V] | BlurEffectEx floats +116..+128 = a1..a4 unconditionally (sub_6C46E0). Exe caller FFX_Opp_Kind07. |
| 823 | `0x643890` | `FFX_MagicApi_BlurExSetPair` | 38 / 0 | [V] | BlurEffectEx floats +108 and +112 = a1, a2 (sub_6C4710). Exe caller FFX_Opp_Kind07. |

### specular chr (4 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 905 | `0x642E00` | `FFX_Gfx_QueueSpecularPassAkaza` | 7 / 0 | [V] | sub_6C5B80 on the specular chr singleton: claims a slot, mode 4 (SpecularPass_nolight_akaza1) when the last arg is 16 else 3 (nolight_akaza2), copies 4 matrices, 1 matrix, 4 floats, a vec4. Pass names at 0xC16AC4/0xC16AD4. 7 DLLs. |
| 912 | `0x642E40` | `FFX_Gfx_QueueSpecularPassAkazaLit` | 2 / 0 | [V] | sub_6C5C90: same slot record with mode 5 (SpecularPass_akaza, 0xC16AE4) and also sets ffxLvMatrix and ffxLcMatrix (0x6C5C90). 2 DLLs. |
| 879 | `0x643770` | `FFX_Gfx_QueueSpecularPassNoLight` | 9 / 0 | [V] | sub_6C5E30: finds or claims a specular chr slot with mode 0 (SpecularPass_nolight, 0xC16AA4), pushes the pass and sets the SpecularMap parameters through sub_6C6390. 9 DLLs. |
| 780 | `0x643980` | `FFX_Gfx_SetSpecularMapParams` | 0 | [V] | No magic DLL calls this slot. Calls sub_6C6830 which writes SpecularMapSampler, SpecularMapSamplerState, SpecularPower, SpecularUvOffset and Shininess on an instance's materials. |

### text and sprites (4 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 802 | `0x8AB570` | `FFX_MesWin_SetLetterboxAlpha` | 6 / 0 | [V] | dword_C59F38 = a1. FFX_MesWin_DrawAllWindows draws the black bars with sub_8F3A50 while it is > 0 (0x8ABD53). 6 DLLs. |
| 814 | `0x8E5CD0` | `FFX_Text_ClearScissor` | 10 / 0 | [V] | dword_18716D0 = 0, byte_18716D4 = 0: turns off the clip rect FFX_Text_ClipQuadToScissor tests (0x8E5A24). Callers FFX_BtlMenu_DrawHelp, rcbgGuideDraw. 10 DLLs. |
| 877 | `0x8E5F60` | `FFX_Sprite_DrawFrameBufferFull` | 0 | [V] | No magic DLL calls this slot. Draws the 'FrameBuffer' texture as one 512x416 sprite, white, through FFX_Sprite2D_Draw (0x8E5F60). |
| 813 | `0x8E8980` | `FFX_Text_SetScissorRect` | 10 / 0 | [V] | Enables the 2D clip rect: dword_18716D0 = 1, rect floats at flt_18716F8..1704 that FFX_Text_ClipQuadToScissor reads (0x8E5A60). Caller FFX_BtlMenu_DrawCtbWindow. 10 DLLs. |

### battle menu (3 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 373 | `0x897770` | `FFX_BtlMenu_GetInfoPanelState` | 1 / 0 | [V] | Returns word_133F6A8, the state FFX_BtlOd_PressCircleGate steps and sub_895390 draws the unit info panel from (0x897770). DLL 0020. |
| 374 | `0x897DC0` | `maybe_FFX_BtlMenu_InitInfoPanel` | 1 / 0 | [I] | Reads layout rects and points dum28, dum29, dumm1..3, dum37/38, dum39, dum30 from menu layout 2, then zeroes the 0x5C byte panel state at word_133F6A8 (0x897DC0). DLL 0020 calls it before 375. |
| 375 | `0x898BD0` | `FFX_BtlMenu_OpenUnitInfoPanel` | 1 / 0 | [V] | word_133F6A8 = 1, word_133F6AA = unit a1, then sub_89A160 fills the unit's panel rows. State 1 starts the panel sequence FFX_BtlOd_PressCircleGate and sub_895390 step (0x898BD0). DLL 0020. |

### CRT and memory (1 slots)

| slot | target | name | DLLs call / thunk | tag | what it is |
| --- | --- | --- | --- | --- | --- |
| 803 | `0x9DBE90` | `FFX_MagicApi_MemAllocAligned` | 182 / 0 | [V] | Forwards (size, align) to sub_686FF0 -> sub_6FB770, which takes the allocator lock, allocates size + align, fills 0xCD and stores the pad byte, the allocation pair of j_j_FFX_MemFreeAlignedLocked (slot 804). magic_0021 slot 0 takes its 1 MB work area here. |

## 9. All 947 slots

State: "named here" with its tag, "named earlier", "guess, earlier" for a `maybe_` name this pass did not touch, "left" for the null. ", never called" marks the 59 slots no DLL reaches.


| slot | target | name | DLLs call / thunk | state |
| --- | --- | --- | --- | --- |
| 0 | `0x824F90` | `FFX_Ch_Allocate` | 28 / 0 | named earlier |
| 1 | `0x832760` | `FFX_Ch_BuildSkinMatrices` | 2 / 0 | named earlier |
| 2 | `0x824650` | `FFX_Ch_MarkDirty` | 0 | named earlier, never called |
| 3 | `0x824670` | `FFX_Ch_ResetPartVertexBuffers` | 164 / 0 | named earlier |
| 4 | `0x829EF0` | `FFX_Ch_RomRead` | 27 / 0 | named earlier |
| 5 | `0x82A040` | `FFX_Ch_DataReadSync` | 9 / 0 | named earlier |
| 6 | `0x82EB60` | `FFX_Ch_SetFlags1Bit2000` | 88 / 0 | named here [V] |
| 7 | `0x82A9A0` | `FFX_Ch_GetI408` | 3 / 0 | named earlier |
| 8 | `0x82A9D0` | `FFX_Ch_SetI408` | 10 / 0 | named earlier |
| 9 | `0x833E30` | `FFX_Ch_SetJointMatrixOverride` | 2 / 0 | named earlier |
| 10 | `0x82AA30` | `FFX_Ch_SetFlags1Bit800` | 25 / 0 | named earlier |
| 11 | `0x82ABC0` | `FFX_Ch_GetGroundMode` | 16 / 0 | named earlier |
| 12 | `0x82AC40` | `FFX_Ch_GetGroundHeight` | 222 / 0 | named earlier |
| 13 | `0x833FC0` | `FFX_Ch_GetScaledWorldMatrix` | 6 / 0 | named here [V] |
| 14 | `0x82AD00` | `FFX_Ch_GetScale` | 115 / 0 | named earlier |
| 15 | `0x82AD30` | `FFX_Ch_GetScaleX` | 73 / 0 | named earlier |
| 16 | `0x82AD40` | `FFX_Ch_GetScaleY` | 88 / 0 | named earlier |
| 17 | `0x82AD50` | `FFX_Ch_GetScaleZ` | 83 / 0 | named earlier |
| 18 | `0x836AF0` | `FFX_Mot_SeqTotalFrames` | 164 / 0 | named earlier |
| 19 | `0x82AD90` | `FFX_Ch_GetDataF20` | 22 / 0 | named earlier |
| 20 | `0x8392C0` | `FFX_Mot_IsRequestActive` | 274 / 0 | named earlier |
| 21 | `0x82AE30` | `FFX_Ch_ModelSetAlpha` | 3 / 0 | named earlier |
| 22 | `0x839860` | `FFX_Ch_GetMotionFrame` | 7 / 0 | named here [V] |
| 23 | `0x82B010` | `FFX_Ch_SetNextHokan` | 158 / 0 | named earlier |
| 24 | `0x824F00` | `FFX_Ch_DataDispose` | 19 / 0 | named earlier |
| 25 | `0x836830` | `FFX_Ch_MotionSetDispose` | 25 / 0 | named earlier |
| 26 | `0x836870` | `FFX_Ch_LoadMotionSetSync` | 28 / 0 | named earlier |
| 27 | `0x836A40` | `FFX_Ch_MotionSetReadStart` | 27 / 0 | named earlier |
| 28 | `0x836A50` | `FFX_Ch_MotionSetReadSync` | 9 / 0 | named earlier |
| 29 | `0x835600` | `FFX_Ch_GetBodyCentre` | 116 / 0 | named here [V] |
| 30 | `0x82B080` | `FFX_Ch_ResetBlock730` | 183 / 0 | named earlier |
| 31 | `0x82B400` | `FFX_Ch_SetF110` | 3 / 0 | named earlier |
| 32 | `0x82B440` | `FFX_Ch_SetPosXZ` | 16 / 0 | named earlier |
| 33 | `0x82B480` | `FFX_Ch_SetPos` | 17 / 0 | named earlier |
| 34 | `0x82B520` | `FFX_Ch_SetRot` | 121 / 0 | named earlier |
| 35 | `0x82B560` | `FFX_Ch_SetScaleMasked` | 117 / 0 | named earlier |
| 36 | `0x837AC0` | `FFX_Ch_SetMotionKey` | 274 / 0 | named earlier |
| 37 | `0x837D00` | `FFX_Mot_SetByModeIndex` | 167 / 0 | named earlier |
| 38 | `0x82B900` | `FFX_Ch_SetShadeAlpha` | 103 / 0 | named earlier |
| 39 | `0x740EB0` | `pppDrawShape_Con` | 0 / 448 | named earlier |
| 40 | `0x75B740` | `pppAccele_Calc` | 0 / 529 | named earlier |
| 41 | `0x75B810` | `pppAccele_Con` | 0 / 529 | named earlier |
| 42 | `0x75B850` | `pppAngAccele_Calc` | 0 / 497 | named earlier |
| 43 | `0x75B8C0` | `pppAngAccele_Con` | 0 / 497 | named earlier |
| 44 | `0x75B900` | `pppSclAccele_Calc` | 0 / 565 | named earlier |
| 45 | `0x75B9D0` | `pppSclAccele_Con` | 0 / 565 | named earlier |
| 46 | `0x75BA40` | `pppColAccele_Calc` | 0 / 575 | named earlier |
| 47 | `0x75BAC0` | `pppColAccele_Con` | 0 / 575 | named earlier |
| 48 | `0x75BDE0` | `pppMove_Calc` | 0 / 554 | named earlier |
| 49 | `0x75BEB0` | `pppMove_Con` | 0 / 554 | named earlier |
| 50 | `0x75BEF0` | `pppAngMove_Calc` | 0 / 543 | named earlier |
| 51 | `0x75BF60` | `pppAngMove_Con` | 0 / 543 | named earlier |
| 52 | `0x75BFA0` | `pppSclMove_Calc` | 0 / 565 | named earlier |
| 53 | `0x75C070` | `pppSclMove_Con` | 0 / 565 | named earlier |
| 54 | `0x75C390` | `pppColMove_Calc` | 0 / 575 | named earlier |
| 55 | `0x75C410` | `pppColMove_Con` | 0 / 575 | named earlier |
| 56 | `0x75CFE0` | `pppScale_Calc` | 0 / 575 | named earlier |
| 57 | `0x75D050` | `pppScale_Con` | 0 / 575 | named earlier |
| 58 | `0x75D1F0` | `pppColor_Calc` | 0 / 575 | named earlier |
| 59 | `0x75D240` | `pppColor_Con` | 0 / 575 | named earlier |
| 60 | `0x75E430` | `pppKeDrct_Calc` | 0 / 114 | named earlier |
| 61 | `0x75E470` | `pppKeDrct_Con` | 0 / 114 | named earlier |
| 62 | `0x730AB0` | `pppRandFV_Calc` | 0 / 518 | named earlier |
| 63 | `0x730CE0` | `pppRandUpFV_Calc` | 0 / 239 | named earlier |
| 64 | `0x730EF0` | `pppRandDownFV_Calc` | 0 / 158 | named earlier |
| 65 | `0x731100` | `pppRandIV_Calc` | 0 / 545 | named earlier |
| 66 | `0x7325F0` | `pppSRandFV_Calc` | 0 / 391 | named earlier |
| 67 | `0x72D390` | `pppMatrixXYZ_Calc` | 0 / 575 | named earlier |
| 68 | `0x72D750` | `pppMatrixYXZ_Calc` | 0 / 60 | named earlier |
| 69 | `0x734130` | `pppMatrixLoc_Calc` | 0 / 113 | named earlier |
| 70 | `0x734170` | `pppMatrixScl_Calc` | 0 / 485 | named earlier |
| 71 | `0x734880` | `pppKeParMatR_Calc` | 0 / 69 | named earlier |
| 72 | `0x734B70` | `pppDrawMatrix_Draw` | 0 / 536 | named earlier |
| 73 | `0x734BD0` | `pppDrawMatrixFront_Draw` | 0 / 568 | named earlier |
| 74 | `0x734EF0` | `pppKeDMatFr_Draw` | 0 / 195 | named earlier |
| 75 | `0x736D80` | `pppKeThTp2_Calc` | 0 / 113 | named earlier |
| 76 | `0x736E60` | `pppKeThTp_Con` | 0 / 113 | named earlier |
| 77 | `0x736E90` | `pppKeThSft_Calc` | 0 / 98 | named earlier |
| 78 | `0x737130` | `pppKeThSft_Con` | 0 / 98 | named earlier |
| 79 | `0x736050` | `pppKeTh_Calc` | 0 / 113 | named earlier |
| 80 | `0x736580` | `pppKeTh_Draw` | 0 / 113 | named earlier |
| 81 | `0x736460` | `pppKeTh_Con` | 0 / 113 | named earlier |
| 82 | `0x7364E0` | `pppKeTh_ReCon` | 0 / 113 | named earlier |
| 83 | `0x736570` | `pppKeTh_Des` | 0 / 113 | named here [V] |
| 84 | `0x737770` | `pppDrawMdlBS_Draw` | 0 / 311 | named earlier |
| 85 | `0x737F40` | `pppDrawMdlTs_Draw` | 0 / 192 | named earlier |
| 86 | `0x7384F0` | `pppDrawMdlTs_Con` | 0 / 192 | named earlier |
| 87 | `0x7409E0` | `pppDrawShape_Group` | 0 / 448 | named earlier |
| 88 | `0x72E790` | `pppKeMdlDtt_Draw` | 0 / 181 | named earlier |
| 89 | `0x72E730` | `pppKeMdlDtt_Con` | 0 / 181 | named earlier |
| 90 | `0x72E760` | `pppKeMdlDtt_ReCon` | 0 / 181 | named earlier |
| 91 | `0x757840` | `pppVertexAp_Calc` | 0 / 442 | named earlier |
| 92 | `0x757B30` | `pppVertexAp_Con` | 0 / 442 | named earlier |
| 93 | `0x758960` | `pppKeBornRnd3_Calc` | 0 / 60 | named earlier |
| 94 | `0x7589D0` | `pppKeBornRnd3_Con` | 0 / 60 | named earlier |
| 95 | `0x758F20` | `pppKeBornRnd5_Calc` | 0 / 300 | named earlier |
| 96 | `0x758F90` | `pppKeBornRnd5_Con` | 0 / 300 | named earlier |
| 97 | `0x759200` | `pppKeBornRnd6_Calc` | 0 / 113 | named earlier |
| 98 | `0x759270` | `pppKeBornRnd6_Con` | 0 / 113 | named earlier |
| 99 | `0x75C450` | `pppPoint_Calc` | 0 / 575 | named earlier |
| 100 | `0x75C4C0` | `pppPoint_Con` | 0 / 575 | named earlier |
| 101 | `0x75CE30` | `pppAngle_Calc` | 0 / 575 | named earlier |
| 102 | `0x75CE80` | `pppAngle_Con` | 0 / 575 | named earlier |
| 103 | `0x736960` | `pppKeThRes32_Con` | 0 / 39 | named earlier |
| 104 | `0x7597B0` | `pppKeGrvTgt_Calc` | 0 / 112 | named earlier |
| 105 | `0x759800` | `pppKeGrvTgt_Con` | 0 / 112 | named earlier |
| 106 | `0x759650` | `pppKeGrvEff_Calc` | 0 / 109 | named earlier |
| 107 | `0x7597A0` | `pppKeGrvEff_Con` | 0 / 109 | named here [V] |
| 108 | `0x732130` | `pppRandUpHCV_Calc` | 0 / 76 | named earlier |
| 109 | `0x732B40` | `pppSRandDownFV_Calc` | 0 / 51 | named earlier |
| 110 | `0x735160` | `pppKeZCrct_Draw` | 0 / 370 | named earlier |
| 111 | `0x7354C0` | `pppKeZCrctShp_Draw` | 0 / 356 | named earlier |
| 112 | `0x738530` | `pppDrawMdl2_Draw` | 0 / 136 | named earlier |
| 113 | `0x7394C0` | `pppDrawMdl3_Draw` | 0 / 319 | named earlier |
| 114 | `0x740ED0` | `pppDrawShapeX_Group` | 0 / 353 | named earlier |
| 115 | `0x741450` | `pppDrawShapeX_Con` | 0 / 353 | named earlier |
| 116 | `0x74C490` | `pppKeShpTail_Calc` | 0 / 47 | named earlier |
| 117 | `0x74C570` | `pppKeShpTail_Draw` | 0 / 47 | named earlier |
| 118 | `0x74C540` | `pppKeShpTail_Con` | 0 / 47 | named earlier |
| 119 | `0x757490` | `pppPointRAp_Calc` | 0 / 37 | named earlier |
| 120 | `0x757750` | `pppPointRAp_Con` | 0 / 37 | named earlier |
| 121 | `0x72EB20` | `pppVtMime_Draw` | 0 / 41 | named earlier |
| 122 | `0x72ED70` | `pppVtMime_Con` | 0 / 41 | named earlier |
| 123 | `0x72EDE0` | `pppVtMime_Des` | 0 / 41 | named earlier |
| 124 | `0x7587B0` | `pppKeBornRnd2_Calc` | 0 / 81 | named earlier |
| 125 | `0x758820` | `pppKeBornRnd2_Con` | 0 / 81 | named earlier |
| 126 | `0x755000` | `pppKeShpDtt_Calc` | 0 / 188 | named here [V] |
| 127 | `0x755010` | `pppKeShpDtt_Con` | 0 / 188 | named earlier |
| 128 | `0x755040` | `pppKeShpDtt_Draw` | 0 / 188 | named earlier |
| 129 | `0x734640` | `pppParMatrix_Calc` | 0 / 395 | named earlier |
| 130 | `0x757FF0` | `pppVertexApLc_Calc` | 0 / 385 | named earlier |
| 131 | `0x758250` | `pppVertexApLc_Con` | 0 / 385 | named earlier |
| 132 | `0x75E310` | `pppKeMvYpEff_Calc` | 0 / 63 | named earlier |
| 133 | `0x734EB0` | `pppKeDMat_Draw` | 0 / 186 | named earlier |
| 134 | `0x738EC0` | `pppDrawMdlTs2_Draw` | 0 / 116 | named earlier |
| 135 | `0x739480` | `pppDrawMdlTs2_Con` | 0 / 116 | named earlier |
| 136 | `0x73A370` | `pppDrawMdlTs3_Draw` | 0 / 289 | named earlier |
| 137 | `0x73AAC0` | `pppDrawMdlTs3_Con` | 0 / 289 | named earlier |
| 138 | `0x7573C0` | `pppPointAp_Calc` | 0 / 152 | named earlier |
| 139 | `0x757470` | `pppPointAp_Con` | 0 / 152 | named earlier |
| 140 | `0x759FD0` | `pppKeLnsArnd_Con` | 0 / 86 | named earlier |
| 141 | `0x759FF0` | `pppKeLnsArnd_Draw` | 0 / 86 | named earlier |
| 142 | `0x75AB30` | `pppKeLnsFls_Con` | 0 / 72 | named earlier |
| 143 | `0x75AB50` | `pppKeLnsFls_Draw` | 0 / 72 | named earlier |
| 144 | `0x75AF30` | `pppKeLnsLp_Con` | 0 / 113 | named earlier |
| 145 | `0x75AF60` | `pppKeLnsLp_ReCon` | 0 / 113 | named earlier |
| 146 | `0x75AF80` | `pppKeLnsLp_Draw` | 0 / 113 | named earlier |
| 147 | `0x759F30` | `pppKeLnsLpSft_Calc` | 0 / 110 | named earlier |
| 148 | `0x759FB0` | `pppKeLnsLpSft_Con` | 0 / 110 | named earlier |
| 149 | `0x74E490` | `pppKeShpTail2_Calc` | 0 / 198 | named earlier |
| 150 | `0x74E540` | `pppKeShpTail2_Con` | 0 / 198 | named earlier |
| 151 | `0x74E570` | `pppKeShpTail2_Draw` | 0 / 198 | named earlier |
| 152 | `0x7502C0` | `pppKeShpTail3_Calc` | 0 / 79 | named earlier |
| 153 | `0x750510` | `pppKeShpTail3_Con` | 0 / 79 | named earlier |
| 154 | `0x7505A0` | `pppKeShpTail3_ReCon` | 0 / 79 | named earlier |
| 155 | `0x750620` | `pppKeShpTail3_Draw` | 0 / 79 | named earlier |
| 156 | `0x731EC0` | `pppRandHCV_Calc` | 0 / 98 | named earlier |
| 157 | `0x75D340` | `pppVertexAttend_Calc` | 0 / 32 | named earlier |
| 158 | `0x7582E0` | `pppVertexApAt_Calc` | 0 / 32 | named earlier |
| 159 | `0x7584B0` | `pppVertexApAt_Con` | 0 / 32 | named earlier |
| 160 | `0x716710` | `FFX_Vfx_ForEachPrim` | 339 / 0 | named here [V] |
| 161 | `0x734CF0` | `pppDrawMatrixWood_Draw` | 0 / 138 | named earlier |
| 162 | `0x734D90` | `pppDrawMatrixWood_PrePass` | 0 / 138 | named earlier |
| 163 | `0x74F4D0` | `pppKeShpTail2X_Calc` | 0 / 47 | named earlier |
| 164 | `0x74F5B0` | `pppKeShpTail2X_Draw` | 0 / 47 | named earlier |
| 165 | `0x74F580` | `pppKeShpTail2X_Con` | 0 / 47 | named earlier |
| 166 | `0x759BF0` | `pppKeHmgEff_Calc` | 0 / 55 | named earlier |
| 167 | `0x759D70` | `pppKeHmgEff_Con` | 0 / 55 | named here [V] |
| 168 | `0x788DE0` | `FFX_Btl_UnitIdPassthrough` | 338 / 0 | named here [V] |
| 169 | `0x7948A0` | `FFX_Btl_GetUnitHomeFacing` | 10 / 0 | named here [V] |
| 170 | `0x795E20` | `FFX_Btl_SetUnitRotAndMoveDir` | 8 / 0 | named here [V] |
| 171 | `0x75A3A0` | `pppKeLnsClm_Draw` | 0 / 39 | named earlier |
| 172 | `0x75A370` | `pppKeLnsClm_Con` | 0 / 39 | named earlier |
| 173 | `0x7E3B70` | `op_oef2_set` | 7 / 0 | named earlier |
| 174 | `0x72F4B0` | `pppRandFloat_Calc` | 0 / 32 | named earlier |
| 175 | `0x72D930` | `pppMatrixYZX_Calc` | 0 / 38 | named earlier |
| 176 | `0x736720` | `pppKeThRes16_Con` | 0 / 33 | named earlier |
| 177 | `0x736B40` | `pppKeThRes48_Con` | 0 / 7 | named earlier |
| 178 | `0x75A760` | `pppKeLnsCrn_Draw` | 0 / 16 | named earlier |
| 179 | `0x75A740` | `pppKeLnsCrn_Con` | 0 / 16 | named earlier |
| 180 | `0x72DB10` | `pppMatrixZXY_Calc` | 0 / 42 | named earlier |
| 181 | `0x7328B0` | `pppSRandUpFV_Calc` | 0 / 35 | named earlier |
| 182 | `0x72DCF0` | `pppMatrixZYX_Calc` | 0 / 15 | named earlier |
| 183 | `0x749810` | `pppKeMdlTfd_Draw` | 0 / 3 | named earlier |
| 184 | `0x7497E0` | `pppKeMdlTfd_Con` | 0 / 3 | named earlier |
| 185 | `0x749FF0` | `pppKeMdlTfdUv3_Draw` | 0 / 10 | named earlier |
| 186 | `0x749F90` | `pppKeMdlTfdUv3_Con` | 0 / 10 | named earlier |
| 187 | `0x736C00` | `pppKeThRes64_Con` | 0 / 16 | named earlier |
| 188 | `0x72EDB0` | `pppVtMime_ReCon` | 0 / 26 | named earlier |
| 189 | `0x751CA0` | `pppKeShpTail3X_Calc` | 0 / 32 | named earlier |
| 190 | `0x751FC0` | `pppKeShpTail3X_Draw` | 0 / 32 | named earlier |
| 191 | `0x751EB0` | `pppKeShpTail3X_Con` | 0 / 32 | named earlier |
| 192 | `0x751F40` | `pppKeShpTail3X_ReCon` | 0 / 32 | named earlier |
| 193 | `0x732390` | `pppRandDownHCV_Calc` | 0 / 25 | named earlier |
| 194 | `0x734C50` | `pppDrawMatrixNoRot_Draw` | 0 / 1 | named earlier |
| 195 | `0x739D30` | `pppDrawMdlSemi3_Draw` | 0 / 36 | named earlier |
| 196 | `0x75A190` | `pppKeLnsArndT_Draw` | 0 / 1 | named earlier |
| 197 | `0x75A0F0` | `pppKeLnsArndT_Con` | 0 / 1 | named earlier |
| 198 | `0x75A140` | `pppKeLnsArndT_Des` | 0 / 1 | named earlier |
| 199 | `0x75A540` | `pppKeLnsClmT_Draw` | 0 / 1 | named earlier |
| 200 | `0x75A4A0` | `pppKeLnsClmT_Con` | 0 / 1 | named earlier |
| 201 | `0x75A4F0` | `pppKeLnsClmT_Des` | 0 / 1 | named earlier |
| 202 | `0x75A900` | `pppKeLnsCrnT_Draw` | 0 / 1 | named earlier |
| 203 | `0x75A870` | `pppKeLnsCrnT_Con` | 0 / 1 | named earlier |
| 204 | `0x75A8B0` | `pppKeLnsCrnT_Des` | 0 / 1 | named earlier |
| 205 | `0x75AD00` | `pppKeLnsFlsT_Draw` | 0 / 1 | named earlier |
| 206 | `0x75AC60` | `pppKeLnsFlsT_Con` | 0 / 1 | named earlier |
| 207 | `0x75ACB0` | `pppKeLnsFlsT_Des` | 0 / 1 | named earlier |
| 208 | `0x7E46B0` | `op_oef2_clut_load` | 11 / 0 | named earlier |
| 209 | `0x7E5030` | `op_oef2_texture_load` | 11 / 0 | named earlier |
| 210 | `0x833F80` | `FFX_Ch_GetWorldMatrix` | 71 / 0 | named earlier |
| 211 | `0x82B3E0` | `FFX_Ch_SetI10C` | 166 / 0 | named earlier |
| 212 | `0x82B820` | `FFX_Ch_SetB183` | 4 / 0 | named earlier |
| 213 | `0x787950` | `maybe_FFX_Btl_ReleaseUnitMotion` | 288 / 0 | named here [I] |
| 214 | `0x788920` | `FFX_Btl_EffectAdvanceState` | 411 / 0 | named earlier |
| 215 | `0x795080` | `FFX_Btl_GetUnitChr` | 364 / 0 | named earlier |
| 216 | `0x7950F0` | `FFX_Btl_GetUnitHeight` | 13 / 0 | named here [V] |
| 217 | `0x7956E0` | `FFX_Btl_GetUnitPos` | 175 / 0 | named here [V] |
| 218 | `0x7B7090` | `FFX_Btl_BuildTargetOrder` | 333 / 0 | named earlier |
| 219 | `0x795B40` | `FFX_Btl_GetUnitSideChr` | 149 / 0 | named here [V] |
| 220 | `0x7FFE10` | `FFX_EffectSnd_Play` | 358 / 0 | named here [V] |
| 221 | `0x8002C0` | `FFX_SpuStream_QueueSeLoadRange` | 369 / 0 | named here [V] |
| 222 | `0x7E4830` | `FFX_Host_QueueTexRange` | 410 / 0 | named earlier |
| 223 | `0x7367B0` | `pppKeThRes16x4_Con` | 0 / 4 | named earlier |
| 224 | `0x7FCBD0` | `maybe_FFX_Effect_EmptyHook` | 0 / 1 | named here [I] |
| 225 | `0x7FCE10` | `FFX_Magic_GetEffectHoldFlag` | 411 / 0 | named earlier |
| 226 | `0x788200` | `FFX_Btl_EffectSetReady` | 411 / 0 | named earlier |
| 227 | `0x787CA0` | `FFX_Btl_GetEffectHitsDone` | 357 / 0 | named earlier |
| 228 | `0x7882C0` | `FFX_Btl_PlayUnitMotion` | 289 / 0 | named here [V] |
| 229 | `0x6ED2E0` | `FFX_Mtx_TransformVec4` | 377 / 0 | named earlier |
| 230 | `0x6ED360` | `FFX_Mtx_Copy` | 406 / 0 | named earlier |
| 231 | `0x6ED5C0` | `FFX_Mtx_Mul_Api` | 408 / 0 | named earlier |
| 232 | `0x6ED6F0` | `FFX_Vec4_Normalize` | 148 / 0 | named earlier |
| 233 | `0x6ED8F0` | `FFX_Vec4_Scale` | 138 / 0 | named earlier |
| 234 | `0x6ED940` | `FFX_Vec4_Sub` | 194 / 0 | named earlier |
| 235 | `0x6ED9A0` | `FFX_Mtx_SetIdentity` | 274 / 0 | named earlier |
| 236 | `0x833C30` | `FFX_Ch_GetJointWorldMatrix` | 159 / 0 | named here [V] |
| 237 | `0x833D10` | `FFX_Ch_GetJointWorldPoint` | 236 / 0 | named here [V] |
| 238 | `0x7966A0` | `FFX_Btl_SetUnitShowVote1` | 30 / 0 | named here [V] |
| 239 | `0x78D980` | `FFX_Btl_ApplyPendingHits` | 275 / 0 | named here [V] |
| 240 | `0x790260` | `FFX_Btl_IsUnitMotionDone_Api` | 5 / 0 | named here [V] |
| 241 | `0x9DBB40` | `FFX_MagicApi_EffectRandFloat` | 3 / 0 | named here [V] |
| 242 | `0x7ED0C0` | `maybe_FFX_Vfx_BuildEmulatedVuModelPacket` | 1 / 0 | named here [I] |
| 243 | `0x72C730` | `FFX_Ppp_RotMatrixX` | 4 / 0 | named earlier |
| 244 | `0x72CB10` | `FFX_Ppp_RotMatrixZ` | 6 / 0 | named earlier |
| 245 | `0x820530` | `FFX_Gs_GetDBuff` | 39 / 0 | named here [V] |
| 246 | `0x82AB80` | `FFX_Ch_GetMoveDir` | 1 / 0 | named earlier |
| 247 | `0x72C920` | `FFX_Ppp_RotMatrixY` | 11 / 0 | named earlier |
| 248 | `0x6ED290` | `FFX_Vec4_Add` | 254 / 0 | named earlier |
| 249 | `0x82B380` | `FFX_Ch_SetSpeedMulA` | 160 / 0 | named earlier |
| 250 | `0x7BC0C0` | `FFX_Came_SetScreenOffset` | 241 / 0 | named earlier |
| 251 | `0x795A20` | `FFX_Btl_GetUnitRadius` | 36 / 0 | named here [V] |
| 252 | `0x7EAAC0` | `maybe_FFX_Vfx_AppendScreenFillPacket_stub` | 2 / 0 | named here [I] |
| 253 | `0x82D7E0` | `FFX_Player__getSubStepCount` | 180 / 0 | named earlier |
| 254 | `0x82A9F0` | `FFX_Ch_SetFlags1Bit100Inv` | 1 / 0 | named earlier |
| 255 | `0x796150` | `FFX_Btl_SetUnitPos` | 176 / 0 | named earlier |
| 256 | `0x82B5D0` | `FFX_Ch_SetShadeColorAnim` | 91 / 0 | named earlier |
| 257 | `0x82B2A0` | `FFX_Ch_SetHideBit2` | 209 / 0 | named earlier |
| 258 | `0x717410` | `FFX_Ppp_BuildPartWorldMatrix` | 62 / 0 | named here [V] |
| 259 | `0x7175F0` | `FFX_Ppp_EmptyHook` | 4 / 0 | named here [V] |
| 260 | `0x86AEE0` | `FFX_Atel_GetPartyCharChr_dup` | 32 / 0 | named here [V] |
| 261 | `0x86B030` | `FFX_Atel_TestSignalFlag` | 45 / 0 | named here [V] |
| 262 | `0x82AC90` | `FFX_Ch_GetPos` | 335 / 0 | named earlier |
| 263 | `0x8303F0` | `FFX_Ch_GetJointCount` | 5 / 0 | named here [V] |
| 264 | `0x7E6A00` | `FFX_Scratch_Push` | 118 / 0 | named earlier |
| 265 | `0x7E6A60` | `FFX_Scratch_Pop` | 118 / 0 | named earlier |
| 266 | `0x6ED720` | `FFX_Vec4_Cross` | 20 / 0 | named earlier |
| 267 | `0x909E10` | `FFX_MagicDraw_ClearFlags` | 354 / 0 | named earlier |
| 268 | `0x8354F0` | `FFX_Ch_GetBoneWorldPos` | 287 / 0 | named earlier |
| 269 | `0x7EA2D0` | `FFX_Spline_SetupCubicXyz` | 7 / 0 | named here [V] |
| 270 | `0x7EA120` | `FFX_Oef2_SplineEvalVec3` | 7 / 0 | named here [V] |
| 271 | `0x909E20` | `FFX_MagicDraw_SetFlags` | 263 / 0 | named earlier |
| 272 | `0x74D490` | `pppKeShpTailX_Calc` | 0 / 2 | named earlier |
| 273 | `0x74D570` | `pppKeShpTailX_Draw` | 0 / 2 | named earlier |
| 274 | `0x74D540` | `pppKeShpTailX_Con` | 0 / 2 | named earlier |
| 275 | `0x826950` | `FFX_Ch_UpdateRenderForMagic573` | 16 / 0 | named earlier |
| 276 | `0x830160` | `FFX_Ch_SetMeshGroupLayer` | 125 / 0 | named here [V] |
| 277 | `0x82AB60` | `FFX_Ch_GetDataF30` | 184 / 0 | named earlier |
| 278 | `0x82AEA0` | `FFX_Ch_ModelSetHide` | 11 / 0 | named earlier |
| 279 | `0x887C30` | `FFX_MesWin_GetTextLanguage` | 4 / 0 | named earlier |
| 280 | `0x7816B0` | `FFX_Btl_GetCurrentEncounterKey` | 2 / 0 | named earlier |
| 281 | `0x79DDB0` | `FFX_BtlSpecial_SetSuspended` | 43 / 0 | named here [V] |
| 282 | `0x7BD510` | `FFX_Came_GetScrDpt` | 15 / 0 | named earlier |
| 283 | `0x7BED60` | `FFX_Came_StopMove` | 43 / 0 | named here [V] |
| 284 | `0x7BF940` | `FFX_Came_SetPos` | 45 / 0 | named earlier |
| 285 | `0x7BFA80` | `FFX_Came_SetRoll` | 43 / 0 | named here [V] |
| 286 | `0x7BFBD0` | `FFX_Came_SetScrDpt` | 44 / 0 | named here [V] |
| 287 | `0x7877D0` | `FFX_Btl_GetCameraHandle` | 43 / 0 | named here [V] |
| 288 | `0x7B6C50` | `FFX_Btl_BuildUnitListByKind` | 109 / 0 | named here [V] |
| 289 | `0x794FB0` | `FFX_Btl_GetUnitGroundHeight` | 122 / 0 | named earlier |
| 290 | `0x795720` | `FFX_Btl_GetUnitScale` | 120 / 0 | named earlier |
| 291 | `0x79A680` | `FFX_Btl_SumPendingHitAmounts` | 229 / 0 | named here [V] |
| 292 | `0x782AE0` | `FFX_Btl_GetBattlePoint` | 9 / 0 | named here [V] |
| 293 | `0x79AEF0` | `FFX_Btl_IsEnemySlot` | 87 / 0 | named earlier |
| 294 | `0x788E10` | `FFX_Btl_GetUnitSummoner` | 9 / 0 | named here [V] |
| 295 | `0x7962B0` | `FFX_Btl_SetUnitB183Request2` | 88 / 0 | named here [V] |
| 296 | `0x796660` | `FFX_Btl_SetUnitShowVote2` | 215 / 0 | named here [V] |
| 297 | `0x9DB270` | `FFX_MagicApi_MtxMulVec4` | 5 / 0 | named here [V] |
| 298 | `0x9DB490` | `FFX_MagicApi_MtxFillRows` | 4 / 0 | named here [V] |
| 299 | `0x9DBA10` | `j_j_FFX_Mtx_Mul_5` | 5 / 0 | named earlier |
| 300 | `0x9DBE10` | `FFX_MagicApi_Sqrt` | 4 / 0 | named here [V] |
| 301 | `0x9DB190` | `FFX_MagicApi_ReturnZero` | 0 | named here [V], never called |
| 302 | `0x888D70` | `FFX_Pad__readButtons16` | 12 / 0 | named earlier |
| 303 | `0x888E50` | `FFX_Pad__readRepeatSet0` | 10 / 0 | named earlier |
| 304 | `0x888E80` | `FFX_Pad__readPressed16` | 10 / 0 | named earlier |
| 305 | `0x887DC0` | `FFX_Pad__getRingLagWindow` | 10 / 0 | named earlier |
| 306 | `0x875FB0` | `FFX_Atel_ShowTextOverlay` | 111 / 0 | named here [V] |
| 307 | `0x798930` | `FFX_Btl_RandFloat1to2` | 81 / 0 | named earlier |
| 308 | `0xA65750` | `op_md_draw` | 4 / 0 | named here [V] |
| 309 | `0x7FBA40` | `FFX_Ch_EffectRegistrySetBit4` | 4 / 0 | named here [V] |
| 310 | `0x7FBAB0` | `FFX_Ch_EffectRegistryClearBit4` | 4 / 0 | named here [V] |
| 311 | `0x7EB300` | `FFX_Vfx_RemoveEffectCallback` | 26 / 0 | named earlier |
| 312 | `0x7EB320` | `FFX_Vfx_AddEffectCallback` | 26 / 0 | named earlier |
| 313 | `0x7E46D0` | `FFX_Tex_IsUploadQueued` | 199 / 0 | named earlier |
| 314 | `0x7E6700` | `FFX_Rand_SignedScale` | 123 / 0 | named earlier |
| 315 | `0x7EC7E0` | `FFX_Vfx_DrawScreenDepthQuad` | 10 / 0 | named here [V] |
| 316 | `0x734020` | `pppKeMatSN_Calc` | 0 / 8 | named earlier |
| 317 | `0x7340F0` | `pppKeMatSN_Con` | 0 / 8 | named earlier |
| 318 | `0x749E60` | `pppKeMdlTfd3_Con` | 0 / 11 | named earlier |
| 319 | `0x749E90` | `pppKeMdlTfd3_Draw` | 0 / 11 | named earlier |
| 320 | `0x7498F0` | `pppKeMdlTfdUv_Con` | 0 / 4 | named earlier |
| 321 | `0x749950` | `pppKeMdlTfdUv_Draw` | 0 / 4 | named earlier |
| 322 | `0x736840` | `pppKeThRes24_Con` | 0 / 39 | named earlier |
| 323 | `0x72D570` | `pppMatrixXZY_Calc` | 0 / 8 | named earlier |
| 324 | `0x7989A0` | `FFX_Btl_Rand` | 92 / 0 | named earlier |
| 325 | `0x6ED370` | `FFX_Vec4_Copy` | 193 / 0 | named earlier |
| 326 | `0x62FD70` | `FFX_Vec3_LerpKeepW` | 44 / 0 | named here [V] |
| 327 | `0x6ED450` | `j_FFX_Mtx_InverseRigid` | 40 / 0 | named earlier |
| 328 | `0x6ED780` | `FFX_Mtx_CopyRotX` | 230 / 0 | named earlier |
| 329 | `0x6ED7C0` | `FFX_Mtx_CopyRotY` | 231 / 0 | named earlier |
| 330 | `0x6ED800` | `FFX_Mtx_CopyRotZ` | 240 / 0 | named earlier |
| 331 | `0x9086D0` | `FFX_MagicDraw_GetClearColor` | 303 / 0 | named here [V] |
| 332 | `0x90A040` | `FFX_MagicDraw_SetClearColor` | 345 / 0 | named here [V] |
| 333 | `0x82ACE0` | `FFX_Ch_GetRotWrapped` | 290 / 0 | named earlier |
| 334 | `0x787760` | `FFX_Btl_CloseAllCameraHandles` | 4 / 0 | named earlier |
| 335 | `0x7FCCC0` | `FFX_Ch_EffectRegistryListChrs` | 1 / 0 | named here [V] |
| 336 | `0x7FCD10` | `FFX_Ch_EffectRegistryFindChrByKey` | 1 / 0 | named here [V] |
| 337 | `0x72C480` | `FFX_PppHeap_Init` | 1 / 0 | named earlier |
| 338 | `0x74A1E0` | `pppNeiDrawMdlPointLight_Draw` | 0 / 2 | named earlier |
| 339 | `0x74AE70` | `pppNeiDrawMdlTsPointLight_Draw` | 0 / 1 | named earlier |
| 340 | `0x74B500` | `pppNeiDrawMdlTsPointLight_Con` | 0 / 1 | named earlier |
| 341 | `0x75D5D0` | `pppNeiLightEikyo_Calc` | 0 / 5 | named earlier |
| 342 | `0x75D6C0` | `pppNeiLightEikyo_Con` | 0 / 5 | named earlier |
| 343 | `0x75D700` | `pppNeiPointLight_Calc` | 0 / 5 | named earlier |
| 344 | `0x75D7C0` | `pppNeiPointLight_Con` | 0 / 5 | named earlier |
| 345 | `0x83C710` | `FFX_Ch_CreateTexListACopy` | 9 / 0 | named here [V] |
| 346 | `0x83C810` | `FFX_Ch_ReleaseTexListACopy` | 9 / 0 | named earlier |
| 347 | `0x795660` | `FFX_Btl_GetUnitMotionEventBits` | 4 / 0 | named here [V] |
| 348 | `0x807870` | `FFX_Oef2_ApplyChrClutHsv` | 9 / 0 | named here [V] |
| 349 | `0x8079F0` | `FFX_Oef2_SaveChrClutHsv` | 9 / 0 | named here [V] |
| 350 | `0x807B20` | `FFX_Oef2_GetChrClutHsvSize` | 5 / 0 | named here [V] |
| 351 | `0x716A30` | `FFX_pppMemAlloc` | 4 / 0 | named here [V] |
| 352 | `0x7EAAD0` | `FFX_Vfx_DrawScreenColorFill` | 2 / 0 | named here [V] |
| 353 | `0x90A270` | `FFX_MagicDraw_SetColorScale` | 347 / 0 | named here [V] |
| 354 | `0x8311D0` | `FFX_Ch_ResetTexListA` | 26 / 0 | named here [V] |
| 355 | `0x831300` | `FFX_Ch_ResetTexListB` | 26 / 0 | named here [V] |
| 356 | `0x831390` | `FFX_Ch_SetTexListAFrame` | 49 / 0 | named here [V] |
| 357 | `0x8313C0` | `FFX_Ch_SetTexListBFrame` | 49 / 0 | named here [V] |
| 358 | `0x9DBAF0` | `FFX_MagicApi_Vec3NormalizeLen` | 1 / 0 | named here [V] |
| 359 | `0x9DBD90` | `FFX_MagicApi_Vec4ScaleXYZ` | 2 / 0 | named here [V] |
| 360 | `0xA68E10` | `maybe_op_md_draw_hier_alt` | 0 | named here [I], never called |
| 361 | `0xA6A3A0` | `maybe_op_md_push_draw_state` | 1 / 0 | named here [I] |
| 362 | `0x803360` | `FFX_Oef2_CaptureChrMeshVisible` | 30 / 0 | named here [V] |
| 363 | `0x8037C0` | `FFX_Oef2_GetChrMeshCaptureSize` | 10 / 0 | named here [V] |
| 364 | `0x82ABA0` | `FFX_Ch_GetHeight` | 38 / 0 | named earlier |
| 365 | `0x82ADB0` | `FFX_Ch_GetF4EC` | 1 / 0 | named earlier |
| 366 | `0x794C20` | `FFX_Btl_GetUnitCenterPos` | 117 / 0 | named here [V] |
| 367 | `0x79AA50` | `FFX_Btl_GetPendingStatusChanges` | 215 / 0 | named here [V] |
| 368 | `0x7B6FB0` | `FFX_Btl_BuildTargetOrderWithAttached` | 2 / 0 | named here [V] |
| 369 | `0x78F950` | `FFX_Btl_GetUnitStatus2` | 1 / 0 | named here [V] |
| 370 | `0x82CD30` | `?_Get_future_error_what@std@@YAPBDH@Z_3` | 1 / 0 | named earlier |
| 371 | `0x82CE20` | `FFX_Gfx_SetDofDepth` | 81 / 0 | named earlier |
| 372 | `0x890EC0` | `FFX_BtlOd_ArmPressCircleGate` | 1 / 0 | named earlier |
| 373 | `0x897770` | `FFX_BtlMenu_GetInfoPanelState` | 1 / 0 | named here [V] |
| 374 | `0x897DC0` | `maybe_FFX_BtlMenu_InitInfoPanel` | 1 / 0 | named here [I] |
| 375 | `0x898BD0` | `FFX_BtlMenu_OpenUnitInfoPanel` | 1 / 0 | named here [V] |
| 376 | `0x7EB3A0` | `FFX_Vfx_RemoveCallbackGroup64` | 6 / 0 | named here [V] |
| 377 | `0x7EB3C0` | `FFX_Vfx_AddCallbackGroup64` | 6 / 0 | named here [V] |
| 378 | `0x7E4C30` | `FFX_Tex_FlushPendingImageLoads` | 84 / 0 | named here [V] |
| 379 | `0x7E4E40` | `FFX_Tex_ReleaseGroup` | 3 / 0 | named here [V] |
| 380 | `0x7E4E80` | `FFX_Tex_ReloadGroup` | 4 / 0 | named here [V] |
| 381 | `0x7EC7D0` | `FFX_Vfx_SetRebuildNodeFreeList` | 6 / 0 | named here [V] |
| 382 | `0x62F880` | `FFX_Ps2_ReadStatStub` | 144 / 0 | named earlier |
| 383 | `0x7EA700` | `FFX_Mtx_SetUniformScale` | 11 / 0 | named here [V] |
| 384 | `0x8266F0` | `FFX_Ch_Dispose` | 28 / 0 | named earlier |
| 385 | `0x82B1E0` | `FFX_Ch_SetB185` | 6 / 0 | named earlier |
| 386 | `0x82B240` | `FFX_Ch_SetGroundMode` | 6 / 0 | named earlier |
| 387 | `0x82B3A0` | `FFX_Ch_SetLocomotionMode` | 6 / 0 | named earlier |
| 388 | `0x78CA20` | `FFX_Btl_PlayUnitReaction` | 20 / 0 | named here [V] |
| 389 | `0x7B7F50` | `FFX_Btl_SetSharedFloatSlot` | 5 / 0 | named here [V] |
| 390 | `0x795610` | `FFX_Btl_GetUnitChrId` | 65 / 0 | named here [V] |
| 391 | `0x82CC20` | `FFX_Sg_SetScreenTint` | 5 / 0 | named here [V] |
| 392 | `0xA8D87A` | `abs` | 0 | named earlier, never called |
| 393 | `0x7FFD20` | `FFX_EffectSnd_PlayAtPos` | 349 / 0 | named here [V] |
| 394 | `0x62FCE0` | `FFX_Vec3_Dot` | 6 / 0 | named earlier |
| 395 | `0x93D830` | `FFX_Mtx_CopyAddTranslation` | 4 / 0 | named here [V] |
| 396 | `0x738A40` | `pppDrawMdlSemi2_Draw` | 0 / 9 | named earlier |
| 397 | `0x909FF0` | `FFX_MagicDraw_SetColorBias` | 230 / 0 | named here [V] |
| 398 | `0x830AE0` | `FFX_Ch_SetLightOverrideMask` | 3 / 0 | named here [V] |
| 399 | `0x835750` | `FFX_Ch_SetFlags1Bit1` | 16 / 0 | named earlier |
| 400 | `0x831950` | `FFX_Light_AllocPointLights` | 3 / 0 | named here [V] |
| 401 | `0x8319B0` | `FFX_Light_SetPoint` | 3 / 0 | named here [V] |
| 402 | `0x831A30` | `FFX_Light_FreePointLights` | 3 / 0 | named here [V] |
| 403 | `0x889B80` | `FFX_Math_DegToRadWrapped` | 10 / 0 | named here [V] |
| 404 | `0x7E6780` | `FFX_Rand_Scale` | 87 / 0 | named earlier |
| 405 | `0x86B050` | `?_Get_future_error_what@std@@YAPBDH@Z_6` | 1 / 0 | named earlier |
| 406 | `0x908DF0` | `FFX_Map_GetPartMatrix` | 1 / 0 | named here [V] |
| 407 | `0x7B72C0` | `FFX_Btl_SortTargetOrderAcrossLine` | 3 / 0 | named here [V] |
| 408 | `0x7FFC60` | `FFX_EffectSnd_UpdateAtPos` | 341 / 0 | named here [V] |
| 409 | `0x7368A0` | `pppKeThRes24x4_Con` | 0 / 28 | named earlier |
| 410 | `0x8355E0` | `FFX_Ch_GetBone0WorldPos` | 1 / 0 | named here [V] |
| 411 | `0x787840` | `FFX_Btl_IsUnitMotionDone` | 277 / 0 | named here [V] |
| 412 | `0x7B7170` | `FFX_Btl_BuildHitTargetList` | 7 / 0 | named here [V] |
| 413 | `0x86AE90` | `FFX_Atel_GetPartyCharChr` | 337 / 0 | named here [V] |
| 414 | `0x7336E0` | `pppSRandHCV_Calc` | 0 / 3 | named earlier |
| 415 | `0x7A9A20` | `FFX_Btl_GetUnitMotionId` | 5 / 0 | named here [V] |
| 416 | `0x736C60` | `pppKeThRes64x4_Con` | 0 / 13 | named earlier |
| 417 | `0x7EB370` | `FFX_Vfx_AddCallbackGroup0` | 1 / 0 | named here [V] |
| 418 | `0x7EB350` | `FFX_Vfx_RemoveCallbackGroup0` | 1 / 0 | named here [V] |
| 419 | `0x736BA0` | `pppKeThRes48x4_Con` | 0 / 7 | named earlier |
| 420 | `0x82CCA0` | `FFX_Sg_StartScreenColorFade` | 1 / 0 | named here [V] |
| 421 | `0x62F4C0` | `maybe_FFX_DebugHook_Stub` | 0 / 3 | named here [I] |
| 422 | `0x75B680` | `pppKeAcmSolid_PostPass` | 0 / 4 | named here [V] |
| 423 | `0x62F7A0` | `maybe_FFX_Gs_LoadImage423_Stub` | 0 / 3 | guess, earlier |
| 424 | `0x62F7B0` | `maybe_FFX_Gs_ImageSync_Stub` | 2 / 0 | named here [I] |
| 425 | `0x62F820` | `maybe_FFX_Gs_LoadImage425_Stub` | 0 / 3 | guess, earlier |
| 426 | `0x62F830` | `FFX_Gs_StoreImage_Stub` | 2 / 0 | named earlier |
| 427 | `0x930430` | `FFX_YanagiTask_WaitUnitMotion` | 0 / 10 | named here [V] |
| 428 | `0x930490` | `FFX_YanagiTask_WaitParticleEnd` | 0 / 10 | named here [V] |
| 429 | `0x9304B0` | `FFX_YanagiTask_WaitEffectHold` | 0 / 10 | named here [V] |
| 430 | `0x9304D0` | `j_maybe_FFX_YanagiTask_PopStateFlag2` | 0, dead thunk | named earlier, never called |
| 431 | `0x930520` | `FFX_YanagiTask_WaitSoleTask` | 0 / 11 | named here [V] |
| 432 | `0x92DF90` | `FFX_Yanagi_EndCasterMotion` | 39 / 0 | named here [V] |
| 433 | `0x92DFB0` | `FFX_Yanagi_EffectSetReady` | 40 / 0 | named here [V] |
| 434 | `0x925170` | `FFX_Yanagi_CreateTask` | 40 / 0 | named earlier |
| 435 | `0x9251C0` | `FFX_Yanagi_FreeProgram` | 40 / 0 | named here [V] |
| 436 | `0x925210` | `FFX_Yanagi_InitProgram` | 40 / 0 | named earlier |
| 437 | `0x925440` | `FFX_Yanagi_StepProgram` | 40 / 0 | named earlier |
| 438 | `0x9254F0` | `FFX_Yanagi_SetPppHeap` | 40 / 0 | named here [V] |
| 439 | `0x925510` | `FFX_Yanagi_SetPppHeapAlt` | 40 / 0 | named here [V] |
| 440 | `0x9256D0` | `FFX_Yanagi_TaskSetParticle` | 38 / 0 | named here [V] |
| 441 | `0x92F970` | `FFX_YanagiTask_SetFollowJoint` | 20 / 0 | named here [V] |
| 442 | `0x92F9A0` | `FFX_YanagiTask_SetFollowBone` | 35 / 0 | named here [V] |
| 443 | `0x930200` | `FFX_YanagiTask_SetDrawFlags` | 32 / 0 | named here [V] |
| 444 | `0x92E680` | `FFX_Yanagi_PlaySound` | 39 / 0 | named here [V] |
| 445 | `0x92E6B0` | `FFX_Yanagi_PlaySoundIfIdle` | 8 / 0 | named here [V] |
| 446 | `0x92E9A0` | `FFX_Yanagi_ApplyPendingHits` | 36 / 0 | named here [V] |
| 447 | `0x92EB10` | `FFX_Yanagi_SetUnitMotion` | 28 / 0 | named here [V] |
| 448 | `0x92EBF0` | `FFX_Yanagi_QueueStreamSound` | 8 / 0 | named here [V] |
| 449 | `0x92ECB0` | `FFX_YanagiTask_PushState` | 40 / 0 | named earlier |
| 450 | `0x92EE20` | `FFX_YanagiTask_SetState` | 40 / 0 | named earlier |
| 451 | `0x92EE60` | `FFX_YanagiTask_WaitFrames` | 37 / 0 | named earlier |
| 452 | `0x949CF0` | `rand_0` | 0 | named earlier, never called |
| 453 | `0x921250` | `rcbgScene_Vec3Length` | 12 / 0 | named here [V] |
| 454 | `0x92D860` | `FFX_Yanagi_HideChrSelfEffects` | 1 / 0 | named here [V] |
| 455 | `0x92D870` | `FFX_Yanagi_ShowChrSelfEffects` | 1 / 0 | named here [V] |
| 456 | `0x92E2F0` | `FFX_Yanagi_GetJointPos` | 10 / 0 | named here [V] |
| 457 | `0x92E340` | `FFX_Yanagi_GetBonePos` | 12 / 0 | named here [V] |
| 458 | `0x925590` | `FFX_Yanagi_StartPartA` | 14 / 0 | named here [V] |
| 459 | `0x92E750` | `FFX_Yanagi_StartUnitTurn1` | 3 / 0 | named here [V] |
| 460 | `0x92E960` | `FFX_Yanagi_StartUnitTurn2` | 3 / 0 | named here [V] |
| 461 | `0x92EE40` | `FFX_YanagiTask_SetStateAndRerun` | 12 / 0 | named here [V] |
| 462 | `0x930500` | `FFX_YanagiTask_WaitSpuStream` | 0, dead thunk | named here [V], never called |
| 463 | `0x92E270` | `FFX_Yanagi_GetBoneMatrixScaled` | 8 / 0 | named here [V] |
| 464 | `0x921360` | `rcbgScene_Vec3Normalize` | 5 / 0 | named here [V] |
| 465 | `0x930550` | `maybe_FFX_Yanagi_CreateChrAfterimage` | 1 / 0 | named here [I] |
| 466 | `0x930600` | `FFX_Yanagi_FreeChrAfterimage` | 1 / 0 | named here [V] |
| 467 | `0x930630` | `maybe_FFX_Yanagi_DrawChrAfterimage` | 1 / 0 | named here [I] |
| 468 | `0x930800` | `FFX_Yanagi_AllocChrAfterimage` | 1 / 0 | named here [V] |
| 469 | `0x92E4F0` | `FFX_Yanagi_RandDirXZ` | 1 / 0 | named here [V] |
| 470 | `0x736A20` | `pppKeThRes32x4_Con` | 0 / 40 | named earlier |
| 471 | `0x9303C0` | `FFX_YanagiTask_WaitScreenFade` | 0, dead thunk | named here [V], never called |
| 472 | `0x92E770` | `FFX_Yanagi_ReverseScreenFade` | 9 / 0 | named here [V] |
| 473 | `0x92E9C0` | `FFX_Yanagi_StartScreenFade` | 10 / 0 | named here [V] |
| 474 | `0x92E0A0` | `FFX_Yanagi_TransformByJoint` | 7 / 0 | named here [V] |
| 475 | `0x737B20` | `pppDrawMdlSemi_Draw` | 0 / 5 | named earlier |
| 476 | `0x92E020` | `FFX_Yanagi_GetJointDirFromRoot` | 1 / 0 | named here [V] |
| 477 | `0x92DFD0` | `FFX_Yanagi_GetUnitRot` | 4 / 0 | named here [V] |
| 478 | `0x92D880` | `FFX_Yanagi_StartMotionBlur` | 3 / 0 | named here [V] |
| 479 | `0x92D9A0` | `FFX_Yanagi_BuildUnitJointMatrixAtPos` | 2 / 0 | named here [V] |
| 480 | `0x92E000` | `FFX_Yanagi_GetUnitBoneNode` | 3 / 0 | named here [V] |
| 481 | `0x92E1C0` | `FFX_Yanagi_GetUnitChr` | 5 / 0 | named here [V] |
| 482 | `0x92E580` | `FFX_Yanagi_Lerp` | 6 / 0 | named here [V] |
| 483 | `0x92E730` | `FFX_Yanagi_ClearChrMatrixOverride` | 3 / 0 | named here [V] |
| 484 | `0x92E830` | `FFX_Yanagi_SetChrMatrixOverride` | 3 / 0 | named here [V] |
| 485 | `0x92EA40` | `FFX_Yanagi_StartScreenFadeFromTo` | 3 / 0 | named here [V] |
| 486 | `0x92EB20` | `FFX_Yanagi_StartUnitMove` | 5 / 0 | named here [V] |
| 487 | `0x930350` | `FFX_YanagiTask_WaitPartA` | 0, dead thunk | named here [V], never called |
| 488 | `0x92E110` | `FFX_Yanagi_GetUnitHeight` | 2 / 0 | named here [V] |
| 489 | `0x92E3A0` | `FFX_Yanagi_GetUnitRadius` | 2 / 0 | named here [V] |
| 490 | `0x925690` | `FFX_Yanagi_StartPartB` | 3 / 0 | named here [V] |
| 491 | `0x92EBA0` | `FFX_Yanagi_StartScreenShake` | 1 / 0 | named here [V] |
| 492 | `0x795840` | `FFX_Btl_GetUnitAiProp8` | 2 / 0 | named here [V] |
| 493 | `0x79E7C0` | `FFX_Btl_GetUnitStatusFxMask` | 1 / 0 | named here [V] |
| 494 | `0x7E9A30` | `FFX_Oef2_RotTransMatrixYXZ` | 1 / 0 | named here [V] |
| 495 | `0x930460` | `FFX_YanagiTask_WaitUnitMoveDone` | 0, dead thunk | named here [V], never called |
| 496 | `0x92D7E0` | `FFX_Yanagi_RadToPs2Rot` | 1 / 0 | named here [V] |
| 497 | `0x92D810` | `FFX_Yanagi_WrapAngle2Pi` | 1 / 0 | named here [V] |
| 498 | `0x92D8C0` | `FFX_Yanagi_BuildUnitJointMatrix` | 1 / 0 | named here [V] |
| 499 | `0x92DD30` | `maybe_FFX_Yanagi_TintChrClut` | 1 / 0 | named here [I] |
| 500 | `0x92DE50` | `FFX_Yanagi_MtxScale001` | 1 / 0 | named here [V] |
| 501 | `0x92DEB0` | `FFX_Yanagi_CopyChrClut` | 1 / 0 | named here [V] |
| 502 | `0x92DF10` | `FFX_Yanagi_ReleaseChrClut` | 1 / 0 | named here [V] |
| 503 | `0x92E170` | `FFX_Yanagi_GetRootDir` | 1 / 0 | named here [V] |
| 504 | `0x92E630` | `FFX_Yanagi_MtxScaleRows100` | 1 / 0 | named here [V] |
| 505 | `0x9301E0` | `FFX_YanagiTask_ClearDrawFlags` | 1 / 0 | named here [V] |
| 506 | `0x92E980` | `j_FFX_Btl_SetUnitPos` | 1 / 0 | named earlier |
| 507 | `0x92E990` | `FFX_Yanagi_SetUnitShowVote` | 1 / 0 | named here [V] |
| 508 | `0x92EB70` | `FFX_Yanagi_SetUnitWeaponVisible` | 1 / 0 | named here [V] |
| 509 | `0x734400` | `pppKeOfsMatXYZ_Calc` | 0 / 8 | named earlier |
| 510 | `0x75CD30` | `pppKeOfsPt_Calc` | 0 / 8 | named earlier |
| 511 | `0x75CDF0` | `pppKeOfsPt_Con` | 0 / 8 | named earlier |
| 512 | `0x9212C0` | `rcbgScene_Vec3Cross` | 1 / 0 | named here [V] |
| 513 | `0x92E360` | `FFX_Yanagi_GetBonePosGround` | 3 / 0 | named here [V] |
| 514 | `0x92E400` | `FFX_Yanagi_AngleDiff` | 3 / 0 | named here [V] |
| 515 | `0x9249A0` | `FFX_Yanagi_GetCasterFacing` | 3 / 0 | named here [V] |
| 516 | `0x9249C0` | `FFX_Yanagi_GetTargetDepth` | 3 / 0 | named here [V] |
| 517 | `0x9249E0` | `maybe_FFX_Yanagi_GetTargetAngle` | 3 / 0 | named here [I] |
| 518 | `0x924A00` | `FFX_Yanagi_GetTargetDistance` | 3 / 0 | named here [V] |
| 519 | `0x924A20` | `FFX_Yanagi_BuildTargetTable` | 3 / 0 | named here [V] |
| 520 | `0x782970` | `FFX_Btl_GetBattleId` | 93 / 0 | named earlier |
| 521 | `0x92E7A0` | `FFX_Yanagi_ReverseTargets` | 0 / 1 | named here [V] |
| 522 | `0x92EC60` | `FFX_Yanagi_ShowText` | 4 / 0 | named here [V] |
| 523 | `0x7B7F30` | `FFX_Btl_SetSharedIntSlot` | 11 / 0 | named here [V] |
| 524 | `0x92E780` | `FFX_Yanagi_EndUnitMotion` | 1 / 0 | named here [V] |
| 525 | `0x7FBCD0` | `FFX_Ch_EndChrEffects` | 86 / 0 | named here [V] |
| 526 | `0x925130` | `FFX_Yanagi_StepScreenFade` | 1 / 0 | named here [V] |
| 527 | `0x925710` | `FFX_Yanagi_SetScreenFadeManual` | 1 / 0 | named here [V] |
| 528 | `0x79FD20` | `FFX_Btl_SetUnitSoundKind6Flag` | 3 / 0 | named here [V] |
| 529 | `0x787C30` | `maybe_FFX_Btl_EffectReturnToIdle` | 158 / 0 | named here [I] |
| 530 | `0x788430` | `FFX_Btl_SetUnitMotionSpeedOverride` | 163 / 0 | named here [V] |
| 531 | `0x7948E0` | `FFX_Btl_GetUnitPlacementPos` | 121 / 0 | named earlier |
| 532 | `0x794D70` | `FFX_Btl_GetUnitAiProp126` | 3 / 0 | named here [V] |
| 533 | `0x794DB0` | `FFX_Btl_TestUnitAiProp125OrPetrified` | 1 / 0 | named here [V] |
| 534 | `0x794F70` | `FFX_Btl_IsUnitAirborne` | 130 / 0 | named here [V] |
| 535 | `0x795640` | `FFX_Btl_GetUnitMoveArcMode` | 3 / 0 | named here [V] |
| 536 | `0x7956A0` | `FFX_Btl_GetUnitAppliedMotionSpeed` | 1 / 0 | named here [V] |
| 537 | `0x795EB0` | `FFX_Btl_StartUnitTurn` | 5 / 0 | named here [V] |
| 538 | `0x796070` | `FFX_Btl_SetUnitGroundMode` | 119 / 0 | named earlier |
| 539 | `0x7960D0` | `FFX_Btl_SetUnitMoveArcMode` | 1 / 0 | named here [V] |
| 540 | `0x796110` | `maybe_FFX_Btl_SetUnitMotionHold` | 2 / 0 | named here [I] |
| 541 | `0x796480` | `FFX_Btl_SetUnitShadeAlpha` | 98 / 0 | named here [V] |
| 542 | `0x7968E0` | `FFX_Btl_SetUnitPushWeightMode` | 2 / 0 | named here [V] |
| 543 | `0x82BEE0` | `FFX_Sg_SetMotionBlur` | 224 / 0 | named here [V] |
| 544 | `0x8360F0` | `FFX_Light_GetColor` | 30 / 0 | named here [V] |
| 545 | `0x836240` | `FFX_Light_GetDirMatrix` | 18 / 0 | named here [V] |
| 546 | `0x7FB290` | `FFX_Ch_StartChrEffects` | 85 / 0 | named here [V] |
| 547 | `0x7FC130` | `FFX_Ch_EffectRegistryEndFlagOverride` | 199 / 0 | named here [V] |
| 548 | `0x7FC190` | `FFX_Ch_EffectRegistryBeginFlagOverride` | 91 / 0 | named here [V] |
| 549 | `0x8000F0` | `FFX_SpuStream_PollRequest` | 216 / 0 | named here [V] |
| 550 | `0x800330` | `FFX_SpuStream_DrainRequests` | 100 / 0 | named here [V] |
| 551 | `0x800390` | `FFX_SpuStream_RequestStream` | 132 / 0 | named here [V] |
| 552 | `0x62FCA0` | `FFX_Vec3_Copy` | 220 / 0 | named earlier |
| 553 | `0x6ED4F0` | `FFX_Mtx_FromRows` | 23 / 0 | named here [V] |
| 554 | `0x93D550` | `FFX_Mtx_RotZYX` | 220 / 0 | named here [V] |
| 555 | `0x908660` | `FFX_MagicDraw_GetColorBias` | 224 / 0 | named here [V] |
| 556 | `0x9088D0` | `FFX_MagicDraw_GetColorScale` | 224 / 0 | named here [V] |
| 557 | `0x908960` | `FFX_Map_GetPartMask` | 8 / 0 | named here [V] |
| 558 | `0x908980` | `FFX_MagicDraw_GetFogColorScaled` | 79 / 0 | named here [V] |
| 559 | `0x908BB0` | `FFX_Map_GetFogRange` | 5 / 0 | named here [V] |
| 560 | `0x90A2B0` | `FFX_Map_SetPartMask` | 8 / 0 | named here [V] |
| 561 | `0x8205E0` | `FFX_Dbg_IsChrUpdateFrozen` | 66 / 0 | named earlier |
| 562 | `0x83B650` | `maybe_FFX_Gs_GetPacketBuffer` | 38 / 0 | named here [I] |
| 563 | `0x7EC430` | `maybe_FFX_Ps2_DmaSendPacketAlt_stub` | 3 / 0 | named here [I] |
| 564 | `0x6ED420` | `FFX_Vec3_DivScalar` | 9 / 0 | named earlier |
| 565 | `0x6ED840` | `FFX_Mtx_TransformPointToFixed` | 135 / 0 | named here [V] |
| 566 | `0x79EAE0` | `FFX_Btl_SetAllUnitsChrEffectsShown` | 3 / 0 | named here [V] |
| 567 | `0xA68C90` | `maybe_op_md_draw_hier` | 7 / 0 | named here [I] |
| 568 | `0x7FF090` | `maybe_FFX_Effect_SetResidentEffectsEnabled` | 4 / 0 | named here [I] |
| 569 | `0x736690` | `pppKeThRes128_Con` | 0 / 29 | named earlier |
| 570 | `0x7371C0` | `pppNeiChrPointLight_Draw` | 0 / 3 | named earlier |
| 571 | `0x7372B0` | `pppNeiChrPointLight_Con` | 0 / 3 | named here [V] |
| 572 | `0x7372C0` | `pppNeiChrPointLight_Des` | 0 / 3 | named here [V] |
| 573 | `0x731560` | `pppRandDownIV_Calc` | 0 / 5 | named earlier |
| 574 | `0x7FC910` | `FFX_Effect_ClearScriptSignal2` | 6 / 0 | named here [V] |
| 575 | `0x7FC900` | `FFX_Effect_SetScriptSignal2` | 4 / 2 | named here [V] |
| 576 | `0x795FF0` | `FFX_Btl_SetUnitHomeFacingOverride` | 2 / 0 | named here [V] |
| 577 | `0x909D30` | `FFX_HostApi_UnusedSlot577` | 0 | named here [V], never called |
| 578 | `0x909D70` | `FFX_HostApi_UnusedSlot578` | 0 | named here [V], never called |
| 579 | `0x833940` | `FFX_Ch_SetEffCallback` | 2 / 0 | named here [V] |
| 580 | `0x79AD90` | `FFX_Btl_GetUnitAiProp267` | 2 / 0 | named here [V] |
| 581 | `0x7EC420` | `maybe_FFX_Ps2_DmaSendPacket_stub` | 13 / 0 | named here [I] |
| 582 | `0x757370` | `pppDrawHook_Draw` | 0 / 10 | named earlier |
| 583 | `0x82CBA0` | `FFX_ScreenFade_RequestMode4` | 1 / 0 | named here [V] |
| 584 | `0x83C9D0` | `FFX_Ch_GetTexListACopyData` | 1 / 0 | named here [V] |
| 585 | `0x83CD20` | `FFX_Ch_GetTexClut` | 1 / 0 | named earlier |
| 586 | `0x83CD90` | `FFX_Ch_GetTexClutCount` | 1 / 0 | named earlier |
| 587 | `0x83D070` | `FFX_Ch_SetTexAnimSlotByteE` | 1 / 0 | named here [V] |
| 588 | `0x8361C0` | `FFX_Light_GetDir` | 8 / 0 | named here [V] |
| 589 | `0x836310` | `FFX_Light_SetColor` | 6 / 0 | named here [V] |
| 590 | `0x836430` | `FFX_Light_SetDir` | 6 / 0 | named here [V] |
| 591 | `0x731350` | `pppRandUpIV_Calc` | 0 / 6 | named earlier |
| 592 | `0x7B7F20` | `FFX_Btl_GetPendingGilCost` | 2 / 0 | named here [V] |
| 593 | `0x7B2B70` | `FFX_Btl_PrepareVoice` | 2 / 0 | named here [V] |
| 594 | `0x7B2BE0` | `FFX_Btl_IsVoiceBusy` | 2 / 0 | named here [V] |
| 595 | `0x7B2BA0` | `FFX_Btl_StartVoice` | 2 / 0 | named here [V] |
| 596 | `0x7AB140` | `FFX_Btl_ClearPath` | 1 / 0 | named here [V] |
| 597 | `0x7AB0B0` | `FFX_Btl_AddPathPoint` | 1 / 0 | named here [V] |
| 598 | `0x7AB010` | `FFX_Btl_StartPathMove` | 1 / 0 | named here [V] |
| 599 | `0x794280` | `FFX_Btl_GetUnitPlaceLayout` | 1 / 0 | named here [V] |
| 600 | `0x795D40` | `FFX_Btl_SetUnitPlaceLayout` | 1 / 0 | named here [V] |
| 601 | `0x794D20` | `FFX_Btl_GetUnitRot` | 118 / 0 | named earlier |
| 602 | `0x82AA70` | `FFX_Ch_SetFlags1Bit80Inv` | 1 / 0 | named earlier |
| 603 | `0x768D30` | `FFX_Vfx_FrameBufferPatch_Setup` | 1 / 0 | named here [V] |
| 604 | `0x767790` | `FFX_Vfx_FrameBufferPatch_Draw` | 1 / 0 | named here [V] |
| 605 | `0x7366C0` | `pppKeThRes128x4_Con` | 0 / 2 | named earlier |
| 606 | `0x82B970` | `FFX_Ch_SetFlags2Bit8` | 1 / 0 | named earlier |
| 607 | `0x749B40` | `pppKeMdlTfd2_Draw` | 0 / 2 | named earlier |
| 608 | `0x749B10` | `pppKeMdlTfd2_Con` | 0 / 2 | named earlier |
| 609 | `0x7341E0` | `pppKeMatPht_Calc` | 0 / 1 | named earlier |
| 610 | `0x735020` | `pppKeDMatPhtFr_Draw` | 0 / 1 | named earlier |
| 611 | `0x753F10` | `pppKeShpTailPht_Calc` | 0 / 1 | named earlier |
| 612 | `0x754300` | `pppKeShpTailPht_Draw` | 0 / 1 | named earlier |
| 613 | `0x754180` | `pppKeShpTailPht_Con` | 0 / 1 | named earlier |
| 614 | `0x754260` | `pppKeShpTailPht_ReCon` | 0 / 1 | named earlier |
| 615 | `0x749C90` | `pppKeMdlTfdUv2_Draw` | 0 / 1 | named earlier |
| 616 | `0x749C30` | `pppKeMdlTfdUv2_Con` | 0 / 1 | named earlier |
| 617 | `0x7B7280` | `FFX_Btl_GetHitTargetList` | 2 / 0 | named here [V] |
| 618 | `0x79A760` | `FFX_Btl_GetNextPendingHitAmounts` | 2 / 0 | named here [V] |
| 619 | `0x729570` | `FFX_MapFx_SuspendGroup` | 1 / 0 | named earlier |
| 620 | `0xA69DE0` | `maybe_op_md_set_packet_refs` | 1 / 0 | named here [I] |
| 621 | `0x8356B0` | `FFX_Ch_GetBonePointJointWorldMatrix` | 1 / 0 | named here [V] |
| 622 | `0x79A860` | `FFX_Btl_GetHitLogReaction` | 10 / 0 | named here [V] |
| 623 | `0x7FC9B0` | `FFX_Ch_AttachChrEffect` | 13 / 0 | named here [V] |
| 624 | `0x7FCA40` | `FFX_Ch_ReleaseChrEffect` | 13 / 0 | named here [V] |
| 625 | `0x795DA0` | `FFX_Btl_SetFlag100VoteAll` | 2 / 0 | named here [V] |
| 626 | `0x788E90` | `maybe_FFX_Btl_ResetUnitsForEffectStart` | 2 / 0 | named here [I] |
| 627 | `0x7B7D00` | `maybe_FFX_Btl_GetTopAggroChar` | 2 / 0 | named here [I] |
| 628 | `0x821CB0` | `FFX_Dbg_SetChrUpdateFrozen` | 16 / 0 | named earlier |
| 629 | `0x7AFDE0` | `FFX_BtlOd_IsMinigamePending` | 6 / 0 | named earlier |
| 630 | `0x7AFD30` | `FFX_BtlOd_StartMinigameForActor` | 6 / 0 | named earlier |
| 631 | `0x7FC9A0` | `FFX_Effect_ClearScriptSignal1` | 1 / 0 | named here [V] |
| 632 | `0x832630` | `FFX_Ch_AttachToParentBone` | 2 / 0 | named earlier |
| 633 | `0x7FC990` | `FFX_Effect_SetScriptSignal1` | 0 / 1 | named here [V] |
| 634 | `0x82B1B0` | `FFX_Ch_SetRotAndMoveDir` | 2 / 0 | named earlier |
| 635 | `0x7B7D70` | `maybe_FFX_Btl_ReturnUnitToField` | 1 / 0 | named here [I] |
| 636 | `0x7B7E20` | `maybe_FFX_Btl_SendUnitOffField` | 1 / 0 | named here [I] |
| 637 | `0x7B7E80` | `FFX_Btl_IsUnitBusy` | 1 / 0 | named here [V] |
| 638 | `0x733A00` | `pppSRandUpHCV_Calc` | 0 / 1 | named earlier |
| 639 | `0x82CBE0` | `FFX_ScreenFade_RequestMode5` | 2 / 0 | named earlier |
| 640 | `0x795780` | `FFX_Btl_IsUnitKO` | 1 / 0 | named here [V] |
| 641 | `0x7ABBA0` | `FFX_SaveData_GetEquippedWeaponChrId` | 20 / 0 | named earlier |
| 642 | `0x741570` | `pppDrawShapeRev_Con` | 0 / 20 | named earlier |
| 643 | `0x7415A0` | `pppDrawShapeRev_Draw` | 0 / 20 | named earlier |
| 644 | `0x736CC0` | `pppKeThRes8_Con` | 0 / 20 | named earlier |
| 645 | `0x7B1910` | `FFX_BtlOd_WakkaReels_GetStrip` | 10 / 0 | named earlier |
| 646 | `0x7B19F0` | `FFX_BtlOd_WakkaReels_GetLevel` | 10 / 0 | named earlier |
| 647 | `0x7B1A70` | `FFX_BtlOd_WakkaReels_PostDone` | 10 / 0 | named earlier |
| 648 | `0x890E70` | `FFX_BtlOd_SetPhaseIdle` | 10 / 0 | named earlier |
| 649 | `0x890F10` | `FFX_BtlOd_WakkaReels_StopCue` | 10 / 0 | named earlier |
| 650 | `0x8974A0` | `FFX_BtlOd_WakkaReels_Draw` | 10 / 0 | named earlier |
| 651 | `0x897780` | `FFX_BtlOd_GetTimeRemaining` | 10 / 0 | named earlier |
| 652 | `0x8977C0` | `FFX_BtlOd_WakkaReels_IsSceneDone` | 10 / 0 | named earlier |
| 653 | `0x8977E0` | `FFX_BtlOd_IsGoodSceneDone` | 10 / 0 | named earlier |
| 654 | `0x897F00` | `FFX_BtlOd_ResetSharedTimer` | 10 / 0 | named earlier |
| 655 | `0x898230` | `FFX_BtlOd_WakkaReels_End` | 10 / 0 | named earlier |
| 656 | `0x898DC0` | `FFX_BtlOd_WakkaReels_Begin` | 10 / 0 | named earlier |
| 657 | `0x89A2A0` | `FFX_BtlOd_SetTimeBudget` | 10 / 0 | named earlier |
| 658 | `0x89A2D0` | `FFX_BtlOd_SetHudPos` | 10 / 0 | named earlier |
| 659 | `0x89AB20` | `FFX_BtlOd_SetPhaseArmed` | 10 / 0 | named earlier |
| 660 | `0x89AB50` | `FFX_BtlOd_SetPhaseRunning` | 10 / 0 | named earlier |
| 661 | `0x89AB80` | `FFX_BtlOd_PlayGoodCue` | 10 / 0 | named earlier |
| 662 | `0x89ABA0` | `FFX_BtlOd_SetPhaseArmed_dup` | 10 / 0 | named earlier |
| 663 | `0x7FFDE0` | `FFX_EffectSnd_FadeOut` | 10 / 0 | named here [V] |
| 664 | `0x794990` | `FFX_Btl_GetUnitYOffset` | 116 / 0 | named here [V] |
| 665 | `0x795980` | `FFX_Btl_GetUnitTopPos` | 116 / 0 | named here [V] |
| 666 | `0x795DD0` | `FFX_Btl_SetUnitRot` | 8 / 0 | named here [V] |
| 667 | `0x6ED3A0` | `FFX_Vec4_DivScalarSafe` | 116 / 0 | named here [V] |
| 668 | `0x6ED5D0` | `FFX_Vec4_MulPerElem` | 116 / 0 | named earlier |
| 669 | `0x90A2C0` | `FFX_MagicDraw_SetFogColor` | 116 / 0 | named earlier |
| 670 | `0x82AFB0` | `FFX_Ch_SetBlendFrames` | 7 / 0 | named earlier |
| 671 | `0x82B2C0` | `FFX_Ch_SetHideBit1` | 3 / 0 | named earlier |
| 672 | `0x7BD470` | `FFX_Came_GetPos` | 2 / 0 | named earlier |
| 673 | `0x7E6B20` | `maybe_FFX_Ps2_FlushCacheStub` | 28 / 0 | named here [I] |
| 674 | `0x7BC0E0` | `FFX_Came_SetScreenOffsetIfActive` | 9 / 0 | named earlier |
| 675 | `0x75B690` | `pppKeAcmCic_Calc` | 0 / 3 | named earlier |
| 676 | `0x764BB0` | `FFX_PppTh_InitNodePool` | 10 / 0 | named here [V] |
| 677 | `0x7644D0` | `FFX_PppTh_SpawnNode` | 12 / 0 | named here [V] |
| 678 | `0x764800` | `FFX_PppTh_InitList` | 10 / 0 | named here [V] |
| 679 | `0x7649A0` | `FFX_PppTh_StepNodes` | 12 / 0 | named here [V] |
| 680 | `0x7645E0` | `FFX_PppTh_DrawNodes` | 12 / 0 | named here [V] |
| 681 | `0x736C30` | `pppKeThRes64x16_Con` | 0 / 2 | named earlier |
| 682 | `0x736A80` | `pppKeThRes40_Con` | 0 / 1 | named earlier |
| 683 | `0x733D10` | `pppSRandDownHCV_Calc` | 0 / 4 | named earlier |
| 684 | `0x787C10` | `FFX_Btl_GetUnitAiProp225` | 9 / 0 | named here [V] |
| 685 | `0x75EDE0` | `FFX_Ppp_Vec4Copy` | 2 / 0 | named here [V] |
| 686 | `0x7BD000` | `FFX_Came_CloseHandle` | 2 / 0 | named earlier |
| 687 | `0x7BD110` | `FFX_Came_OpenHandle` | 2 / 0 | named earlier |
| 688 | `0x7883F0` | `maybe_FFX_Btl_HoldUnitMotion` | 3 / 0 | named here [I] |
| 689 | `0x788DD0` | `FFX_Btl_GetUnit13PartFlag` | 2 / 0 | named here [V] |
| 690 | `0x7BC750` | `FFX_Came_SetScreenDrwRect` | 2 / 0 | named here [V] |
| 691 | `0x7BC890` | `FFX_Came_SetScreenRect` | 2 / 0 | named earlier |
| 692 | `0x7BC950` | `FFX_Came_SetScreenEnableAndSlot` | 2 / 0 | named earlier |
| 693 | `0x82BD10` | `FFX_Sg_SetAccBufAlpha` | 2 / 0 | named here [V] |
| 694 | `0x82BDC0` | `FFX_Sg_SetOffscreenClear` | 3 / 0 | named here [V] |
| 695 | `0x7FBB90` | `FFX_Ch_KillChrEffects` | 2 / 0 | named here [V] |
| 696 | `0x790290` | `FFX_Btl_PlayUnitMotion_Api` | 2 / 0 | named here [V] |
| 697 | `0xA69300` | `op_md_draw_simple` | 1 / 0 | named here [V] |
| 698 | `0x784FF0` | `FFX_Btl_IsCharSwitchBlocked` | 1 / 0 | named here [V] |
| 699 | `0x759D80` | `pppKeThHitBorn_Calc` | 0 / 1 | named earlier |
| 700 | `0x759F10` | `pppKeThHitBorn_Con` | 0 / 1 | named earlier |
| 701 | `0x7AB540` | `FFX_Btl_StartUnitMove` | 3 / 0 | named earlier |
| 702 | `0x795C90` | `FFX_Btl_AttachUnitToUnitBone` | 2 / 0 | named here [V] |
| 703 | `0x794E80` | `FFX_Btl_GetUnitAiProp128` | 1 / 0 | named here [V] |
| 704 | `0x803220` | `FFX_Oef2_CaptureChrMeshAll` | 4 / 0 | named here [V] |
| 705 | `0x8360A0` | `FFX_Map_GetWaterLevel` | 1 / 0 | named here [V] |
| 706 | `0x75FDA0` | `FFX_Ppp_Vec3Length` | 2 / 0 | named here [V] |
| 707 | `0x82CB60` | `FFX_Gfx_SetDofEnabled` | 2 / 0 | named earlier |
| 708 | `0x729660` | `FFX_MapFx_StopGroup` | 1 / 0 | named earlier |
| 709 | `0x729700` | `FFX_MapFx_ResetGroup` | 1 / 0 | named earlier |
| 710 | `0x7169D0` | `FFX_Ppp_InitHeapForSet` | 537 / 0 | named earlier |
| 711 | `0x711FA0` | `FFX_Vfx_RelocateParticleSet` | 537 / 0 | named earlier |
| 712 | `0x712250` | `FFX_Ppp_StepPart` | 537 / 0 | named earlier |
| 713 | `0x711F20` | `FFX_Vfx_FreePrimList` | 532 / 116 | named here [V] |
| 714 | `0x7123C0` | `FFX_Ppp_StartPart` | 537 / 0 | named earlier |
| 715 | `0x7FDA80` | `FFX_Effect_InitUnit` | 130 / 0 | named earlier |
| 716 | `0x80CC00` | `FFX_Magic_Oef2Dispatch` | 130 / 0 | named earlier |
| 717 | `0x7FDC80` | `FFX_Effect_DebugReloadFromHost` | 130 / 0 | named here [V] |
| 718 | `0x7E3E30` | `FFX_Oef2_SetFromUnit` | 541 / 0 | named earlier |
| 719 | `0x7E3810` | `FFX_Oef2_GetSubTable` | 536 / 0 | named earlier |
| 720 | `0x9DBEA0` | `FFX_MagicApi_EmptyHook` | 354 / 0 | named here [V] |
| 721 | `0x80BD50` | `opp_main` | 130 / 0 | named earlier |
| 722 | `0x7FF2B0` | `FFX_Effect_DebugSetMeterSlot` | 130 / 0 | named here [V] |
| 723 | `0x7BBA50` | `FFX_Came_GetProjMatrix` | 539 / 0 | named earlier |
| 724 | `0x7C0070` | `FFX_Came_GetViewMatrix` | 539 / 0 | named earlier |
| 725 | `0x94964C` | `memset` | 0 | named earlier, never called |
| 726 | `0x949F50` | `vprintf` | 142 / 0 | named earlier |
| 727 | `0x62F510` | `maybe_FFX_DebugHalt_Stub` | 130 / 0 | named here [I] |
| 728 | `0xA73810` | `maybe_u2_part_model_draw` | 0 / 581 | named here [I] |
| 729 | `0xA74410` | `maybe_u2_part_model_init` | 0 / 581 | named here [I] |
| 730 | `0xA73D90` | `maybe_u2_part_wave_model` | 0 / 581 | named here [I] |
| 731 | `0xA74A00` | `u2_part_work_scale` | 0 / 581 | named earlier |
| 732 | `0xA74C60` | `maybe_u2_part_big_work` | 0 / 581 | named here [I] |
| 733 | `0xA74D30` | `maybe_u2_part_texture` | 0 / 581 | named here [I] |
| 734 | `0xA756E0` | `maybe_u2_part_chr_center` | 0 / 581 | named here [I] |
| 735 | `0x7F5F70` | `maybe_FFX_Oef2_DllOp_DrawTexMeshOrBranch` | 0 / 581 | named here [I] |
| 736 | `0xA6F4D0` | `maybe_u2_part_chr_shader` | 0 / 581 | named here [I] |
| 737 | `0xA76720` | `maybe_u2_part_reset192` | 0 / 581 | named here [I] |
| 738 | `0xA76B60` | `maybe_u2_part_chr_light` | 0 / 581 | named here [I] |
| 739 | `0xA6DBE0` | `maybe_u2_part_line_model` | 0 / 581 | named here [I] |
| 740 | `0xA70910` | `maybe_u2_part_chr_break` | 0 / 581 | named here [I] |
| 741 | `0x230FFE0` | `g_ffxPppDefaultHeap` | copied by 581 | named here [V] |
| 742 | `0x2332E8C` | `g_ffxEffectPacketCursorA` | copied by 581 | named here [V] |
| 743 | `0x12A40C0` | `g_ffxEffectUnitPtrs` | 0 | named earlier, never called |
| 744 | `0x113FCD0` | `g_ffxEffectViewMatrix` | copied by 581 | named earlier |
| 745 | `0x113FD10` | `g_ffxEffectProjMatrix` | copied by 581 | named earlier |
| 746 | `0x2321540` | `g_ffxEffectRgSettings` | copied by 581 | named here [V] |
| 747 | `0x119FE90` | `g_ffxBattleEffectUnit` | copied by 581 | named here [V] |
| 748 | `0x2322780` | `g_ffxEffectDebugState` | copied by 581 | named here [V] |
| 749 | `0x22FB450` | `g_ffxMagicDllSharedBlock` | copied by 581 | named here [M] |
| 750 | `0x11A0060` | `g_ffxEffectSysFlags` | copied by 581 | named here [V] |
| 751 | `0xC44BF0` | `g_ffxSinTable4096` | copied by 581 | named here [V] |
| 752 | `0x230FF60` | `g_ffxVfxViewMatrix` | copied by 581 | named earlier |
| 753 | `0x230FD2C` | `g_ffxVfxPacketCursor` | copied by 581 | named earlier |
| 754 | `0x230FFA0` | `g_ffxPppPartWorldMatrix` | copied by 581 | named here [V] |
| 755 | `0x230FE20` | `g_ffxPppDrawMatrix` | copied by 581 | named here [V] |
| 756 | `0x230FE60` | `g_ffxVfxProjMatrix` | copied by 581 | named earlier |
| 757 | `0x230FF20` | `g_ffxPppModelViewMatrix` | copied by 581 | named here [V] |
| 758 | `0x230FD34` | `g_ffxVfxTimeFrozen` | copied by 581 | named earlier |
| 759 | `0xC498DC` | `g_ffxSharedWorkBufPtr` | copied by 581 | named here [V] |
| 760 | `0x25D5A20` | `g_ffxYanagiCtx` | copied by 581 | named here [V] |
| 761 | `0x230FD30` | `g_ffxPppDrawHookTable` | copied by 581 | named here [V] |
| 762 | `0x230FD50` | `g_ffxPppOwnerScale` | copied by 581 | named here [V] |
| 763 | `0x11A0710` | `g_ffxGsDrawEnvPacket` | copied by 581 | named here [V] |
| 764 | `0x23CBBF0` | `g_ffxMainStepCounter` | copied by 581 | named earlier |
| 765 | `0xC3E6C0` | `g_ffxClutSwizzleTable` | copied by 581 | named here [V] |
| 766 | `0x9DB1F0` | `FFX_HostApi_UnusedSlot766` | 0 | named here [V], never called |
| 767 | `0x9DB1A0` | `FFX_HostApi_UnusedSlot767` | 0 | named here [V], never called |
| 768 | `0x9DB1B0` | `FFX_HostApi_UnusedSlot768` | 0 | named here [V], never called |
| 769 | `0xC64CE0` | `maybe_g_ffxMagicHostApiVersion` | copied by 581 | named here [I] |
| 770 | `0x643900` | `FFX_ScreenFade_SetColor` | 0 | named earlier, never called |
| 771 | `0x9DB230` | `FFX_MagicApi_MakeOpHandle64` | 8 / 0 | named here [V] |
| 772 | `0x9DB250` | `FFX_MagicApi_MakeHandle64` | 123 / 0 | named here [V] |
| 773 | `0x6453D0` | `FFX_Vfx_GetModelRecord` | 232 / 0 | named here [V] |
| 774 | `0x71B8A0` | `FFX_Vfx_QueueSpriteBuild` | 123 / 0 | named here [V] |
| 775 | `0x6458A0` | `FFX_Vfx_SubmitModel` | 231 / 0 | named earlier |
| 776 | `0x712C30` | `FFX_Vfx_AlphaFuncToBlend` | 249 / 0 | named here [V] |
| 777 | `0x712D00` | `FFX_Vfx_BoolFromInt` | 229 / 0 | named here [V] |
| 778 | `0x711460` | `FFX_Vfx_DrawShp` | 123 / 0 | named here [V] |
| 779 | `0x645280` | `FFX_DynGeoModel_DestroyGroupByKey` | 220 / 0 | named earlier |
| 780 | `0x643980` | `FFX_Gfx_SetSpecularMapParams` | 0 | named here [V], never called |
| 781 | `0x643150` | `FFX_Fog_SetColor` | 4 / 0 | named here [V] |
| 782 | `0x6431E0` | `FFX_Fog_SetNear` | 4 / 0 | named here [V] |
| 783 | `0x6431A0` | `FFX_Fog_SetFar` | 4 / 0 | named here [V] |
| 784 | `0x643180` | `FFX_Fog_SetCurve` | 4 / 0 | named here [V] |
| 785 | `0x6431C0` | `FFX_Fog_SetLimit` | 345 / 0 | named here [V] |
| 786 | `0x641160` | `FFX_App_GetFogColor` | 4 / 0 | named here [V] |
| 787 | `0x6411D0` | `FFX_App_GetFogNear` | 4 / 0 | named here [V] |
| 788 | `0x6411B0` | `FFX_App_GetFogFar` | 4 / 0 | named here [V] |
| 789 | `0x6411A0` | `FFX_App_GetFogCurve` | 4 / 0 | named here [V] |
| 790 | `0x6411C0` | `FFX_App_GetFogLimit` | 302 / 0 | named here [V] |
| 791 | `0x643270` | `FFX_App_SetMagicFogColor` | 116 / 0 | named here [V] |
| 792 | `0x641230` | `FFX_App_GetMagicFogColor` | 1 / 0 | named here [V] |
| 793 | `0x643320` | `FFX_App_SetFloat208` | 0 | named here [V], never called |
| 794 | `0x6432E0` | `FFX_App_SetFloat212` | 0 | named here [V], never called |
| 795 | `0x6432C0` | `FFX_App_SetFloat216` | 0 | named here [V], never called |
| 796 | `0x643300` | `FFX_App_SetFloat220` | 0 | named here [V], never called |
| 797 | `0x6412A0` | `FFX_App_GetFloat208` | 0 | named here [V], never called |
| 798 | `0x641280` | `FFX_App_GetFloat212` | 0 | named here [V], never called |
| 799 | `0x641270` | `FFX_App_GetFloat216` | 0 | named here [V], never called |
| 800 | `0x641290` | `FFX_App_GetFloat220` | 0 | named here [V], never called |
| 801 | `0x643630` | `FFX_Gfx_MulMapColorScale` | 121 / 0 | named here [V] |
| 802 | `0x8AB570` | `FFX_MesWin_SetLetterboxAlpha` | 6 / 0 | named here [V] |
| 803 | `0x9DBE90` | `FFX_MagicApi_MemAllocAligned` | 182 / 0 | named here [V] |
| 804 | `0x9DBE80` | `j_j_FFX_MemFreeAlignedLocked` | 180 / 0 | named earlier |
| 805 | `0x9DB1C0` | `FFX_HostApi_UnusedSlot805` | 0 | named here [V], never called |
| 806 | `0x9DB200` | `FFX_HostApi_UnusedSlot806` | 0 | named here [V], never called |
| 807 | `0x9DB210` | `FFX_HostApi_UnusedSlot807` | 0 | named here [V], never called |
| 808 | `0x7B1B50` | `MsSetReel` | 10 / 0 | named here [V] |
| 809 | `0x9066D0` | `FFX_Magic_SetSpriteVertexPos` | 10 / 0 | named here [V] |
| 810 | `0x9066B0` | `FFX_Magic_SetSpriteVertexUv` | 10 / 0 | named here [V] |
| 811 | `0x906680` | `FFX_Magic_SetSpriteVertexColor` | 10 / 0 | named here [V] |
| 812 | `0x906420` | `FFX_Magic_DrawSpriteTex0` | 10 / 0 | named earlier |
| 813 | `0x8E8980` | `FFX_Text_SetScissorRect` | 10 / 0 | named here [V] |
| 814 | `0x8E5CD0` | `FFX_Text_ClearScissor` | 10 / 0 | named here [V] |
| 815 | `0x711830` | `maybe_FFX_Vfx_DrawMdlImmediate` | 10 / 0 | named here [I] |
| 816 | `0x80A4A0` | `maybe_FFX_Effect_PostInitUnitNop` | 0 / 19 | named here [I] |
| 817 | `0x80A460` | `maybe_FFX_Effect_PostFreeNop` | 2 / 14 | named here [I] |
| 818 | `0x642110` | `FFX_ChrInstance_SetVertexColorHeight` | 2 / 0 | named here [V] |
| 819 | `0x718610` | `FFX_Vfx_QueueModelBuild` | 121 / 0 | named here [V] |
| 820 | `0x711180` | `FFX_Vfx_DrawMdl` | 113 / 0 | named here [V] |
| 821 | `0x639110` | `FFX_MagicApi_BlurExRequest` | 54 / 0 | named here [V] |
| 822 | `0x643830` | `FFX_MagicApi_BlurExSetPositiveParams` | 38 / 0 | named here [V] |
| 823 | `0x643890` | `FFX_MagicApi_BlurExSetPair` | 38 / 0 | named here [V] |
| 824 | `0x643860` | `FFX_MagicApi_BlurExSetVec4` | 38 / 0 | named here [V] |
| 825 | `0x826B70` | `FFX_Ch_GetRenderInstance` | 14 / 0 | named earlier |
| 826 | `0x9DB1D0` | `FFX_HostApi_UnusedSlot826` | 0 | named here [V], never called |
| 827 | `0x113335C` | `g_ffxInMagicOverlayCall` | copied by 581 | named earlier |
| 828 | `0x7117C0` | `maybe_FFX_Vfx_DrawFrameBufferMesh` | 2 / 0 | named here [I] |
| 829 | `0x712EC0` | `FFX_Vfx_CreateTriPairModels` | 1 / 0 | named here [V] |
| 830 | `0x712690` | `FFX_Vfx_AppendTriPair` | 1 / 0 | named here [V] |
| 831 | `0x713160` | `FFX_Vfx_CreateUntexturedQuadModel` | 3 / 0 | named here [V] |
| 832 | `0x7128D0` | `FFX_Vfx_AppendUntexturedQuad` | 3 / 0 | named here [V] |
| 833 | `0x63B810` | `FFX_ChrInstance_SetAlpha` | 14 / 0 | named here [V] |
| 834 | `0x63B9E0` | `maybe_FFX_ChrInstance_ClearAlphaThreshold` | 2 / 0 | named here [I] |
| 835 | `0x63C9E0` | `FFX_ChrInstance_SetAddLightColour` | 0 | named here [V], never called |
| 836 | `0x63CA50` | `FFX_ChrInstance_SetMaterialAndAddLightColour` | 0 | named here [V], never called |
| 837 | `0x63CB00` | `FFX_ChrLight_SetLightColour` | 0 | named here [V], never called |
| 838 | `0x63CDD0` | `FFX_ChrInstance_SetShadeCoeff` | 0 | named earlier, never called |
| 839 | `0x63D050` | `FFX_ChrInstance_SetShadeColor` | 0 | named earlier, never called |
| 840 | `0x645400` | `FFX_Host_ResolveTexByName` | 0 | named earlier, never called |
| 841 | `0x6436A0` | `FFX_Gfx_MulMapColorScaleAlt` | 0 | named here [V], never called |
| 842 | `0x63C430` | `FFX_ChrInstance_SetMeshTransformById` | 2 / 0 | named here [V] |
| 843 | `0x63C3E0` | `FFX_ChrInstance_SetRootTransform` | 19 / 0 | named earlier |
| 844 | `0x830CD0` | `FFX_Ch_SetLayerByteAndBreak` | 0 | named earlier, never called |
| 845 | `0x63DF50` | `FFX_Gfx_ChrClusterToLayerWorld` | 4 / 2 | named here [V] |
| 846 | `0x639350` | `FFX_Gfx_ChrClusterToMainWorld` | 2 / 3 | named here [V] |
| 847 | `0x63B7F0` | `FFX_ChrInstance_SetWord28` | 14 / 0 | named here [V] |
| 848 | `0x643B80` | `FFX_HostApi_UnusedSlot848` | 0 | named here [V], never called |
| 849 | `0x82B260` | `FFX_Ch_SetHideBit10` | 2 / 0 | named earlier |
| 850 | `0xCCCA58` | `g_ffxVfxDynGeoForceBlend4` | copied by 581 | named here [V] |
| 851 | `0x9DB220` | `FFX_HostApi_UnusedSlot851` | 0 | named here [V], never called |
| 852 | `0x826DB0` | `FFX_Ch_ResetLiveChrIter` | 1 / 0 | named earlier |
| 853 | `0x826D70` | `FFX_Ch_NextLiveChr` | 1 / 0 | named earlier |
| 854 | `0x0` | `(null)` | 0 | left [V], never called |
| 855 | `0x63D810` | `FFX_Chr_ApplyMagicShader` | 4 / 0 | named here [V] |
| 856 | `0x63D870` | `FFX_Chr_RestoreAllMagicShaders` | 3 / 1 | named here [V] |
| 857 | `0x63D940` | `FFX_Chr_SetMaterialVec4` | 4 / 0 | named here [V] |
| 858 | `0x642FE0` | `FFX_Gfx_SetOrthoCapture` | 9 / 0 | named here [V] |
| 859 | `0x6438C0` | `FFX_Gfx_SetOrthoCaptureTexId` | 4 / 0 | named here [V] |
| 860 | `0x7116D0` | `FFX_Vfx_DrawFrameBufferNoiseStrips` | 1 / 0 | named here [V] |
| 861 | `0x9DB180` | `j_memcpy` | 4 / 0 | named earlier |
| 862 | `0x711750` | `FFX_Vfx_DrawFrameBufferGrid` | 2 / 0 | named here [V] |
| 863 | `0x6429F0` | `FFX_MagicApi_RenderDelegationAdd` | 0 | named here [V], never called |
| 864 | `0x645E60` | `FFX_MagicApi_QueueScreenFadeClear` | 0 | named here [V], never called |
| 865 | `0x645BA0` | `FFX_Vfx_SubmitModelMode` | 2 / 0 | named earlier |
| 866 | `0x9DB1E0` | `FFX_HostApi_UnusedSlot866` | 0 | named here [V], never called |
| 867 | `0xA5BF50` | `FFX_Vfx_DrawModelScroll` | 2 / 0 | named here [V] |
| 868 | `0x6438E0` | `FFX_ScreenFade_SetBlendMode` | 0 | named earlier, never called |
| 869 | `0x7BD300` | `FFX_Came_GetActiveHandle` | 2 / 0 | named earlier |
| 870 | `0x645600` | `FFX_Vfx_SubmitModelOrdered` | 9 / 0 | named here [V] |
| 871 | `0x710FD0` | `FFX_Vfx_DrawMdlOrdered` | 8 / 0 | named here [V] |
| 872 | `0x711030` | `FFX_Vfx_DrawShpOrdered` | 0 | named here [V], never called |
| 873 | `0x711500` | `FFX_Vfx_DrawSingleSprite` | 7 / 0 | named here [V] |
| 874 | `0x7110D0` | `FFX_Vfx_DrawSingleSpriteOrdered` | 4 / 0 | named here [V] |
| 875 | `0x71C0B0` | `FFX_Vfx_QueueSingleSpriteBuild` | 7 / 0 | named here [V] |
| 876 | `0x699080` | `FFX_HostApi_UnusedSlot876` | 0 | named here [V], never called |
| 877 | `0x8E5F60` | `FFX_Sprite_DrawFrameBufferFull` | 0 | named here [V], never called |
| 878 | `0x641E40` | `FFX_Gfx_SetSceneClearMode` | 3 / 0 | named here [V] |
| 879 | `0x643770` | `FFX_Gfx_QueueSpecularPassNoLight` | 9 / 0 | named here [V] |
| 880 | `0x230FCA0` | `g_ffxVfxInvViewMatrix` | copied by 581 | named earlier |
| 881 | `0x644DE0` | `FFX_Vfx_CreateModel` | 3 / 0 | named earlier |
| 882 | `0x645040` | `FFX_Vfx_SetModelParts` | 3 / 0 | named earlier |
| 883 | `0x645780` | `FFX_Vfx_BuildModelGeometry` | 3 / 0 | named earlier |
| 884 | `0x645710` | `FFX_Vfx_SetModelLinked` | 3 / 0 | named here [V] |
| 885 | `0x645450` | `FFX_Host_ResolveTexByTex0` | 19 / 0 | named earlier |
| 886 | `0x63FDB0` | `FFX_DynGeoMem_AllocModelPart` | 8 / 0 | named here [V] |
| 887 | `0x63FC50` | `FFX_VfxModel_GetPart` | 3 / 0 | named here [V] |
| 888 | `0x63FC70` | `FFX_VfxModel_GetPartCount` | 0 | named here [V], never called |
| 889 | `0x63FD30` | `FFX_VfxModel_SetPartCount` | 0 | named here [V], never called |
| 890 | `0x63FC40` | `FFX_VfxModel_GetBoundsMin` | 3 / 0 | named here [V] |
| 891 | `0x63FC30` | `FFX_VfxModel_GetBoundsMax` | 3 / 0 | named here [V] |
| 892 | `0x63FD50` | `FFX_VfxModel_SetZCorrect` | 3 / 0 | named here [V] |
| 893 | `0x63FC80` | `unknown_libname_330` | 3 / 0 | named earlier |
| 894 | `0x63FCF0` | `FFX_VfxPart_GetVertexCount` | 0 | named here [V], never called |
| 895 | `0x63FD10` | `FFX_VfxPart_SetVertexCount` | 0 | named here [V], never called |
| 896 | `0x63FCB0` | `FFX_VfxPart_GetIndexCount` | 0 | named here [V], never called |
| 897 | `0x63FD00` | `FFX_VfxPart_SetIndexCount` | 0 | named here [V], never called |
| 898 | `0x63FCE0` | `FFX_VfxPart_GetPositions` | 3 / 0 | named here [V] |
| 899 | `0x63FCC0` | `FFX_VfxPart_GetWord16` | 0 | named here [V], never called |
| 900 | `0x63FC90` | `FFX_VfxPart_GetColours` | 3 / 0 | named here [V] |
| 901 | `0x63FCD0` | `FFX_VfxPart_GetTexCoords` | 2 / 0 | named here [V] |
| 902 | `0x63FCA0` | `FFX_VfxPart_GetIndices` | 3 / 0 | named here [V] |
| 903 | `0x63FD20` | `FFX_VfxPart_SetSamplerState` | 2 / 0 | named here [V] |
| 904 | `0x644BE0` | `FFX_GfxCtx_PushCopyCommandUnlessCatchUp_00` | 1 / 0 | named earlier |
| 905 | `0x642E00` | `FFX_Gfx_QueueSpecularPassAkaza` | 7 / 0 | named here [V] |
| 906 | `0x63D8E0` | `FFX_Chr_SetMaterialTexture` | 1 / 0 | named here [V] |
| 907 | `0x7113D0` | `FFX_Vfx_CreateShpBatchModel` | 5 / 0 | named here [V] |
| 908 | `0x71A4E0` | `FFX_Vfx_AppendShpGeometry` | 5 / 0 | named here [V] |
| 909 | `0x713290` | `FFX_Vfx_ResetModelPartCounts` | 1 / 0 | named here [V] |
| 910 | `0x712BD0` | `FFX_Vfx_ReserveShpCounts` | 1 / 0 | named here [V] |
| 911 | `0x7FA6D0` | `FFX_Oef2_DllOp_ScreenWarpMesh` | 0 / 581 | named here [V] |
| 912 | `0x642E40` | `FFX_Gfx_QueueSpecularPassAkazaLit` | 2 / 0 | named here [V] |
| 913 | `0x639170` | `FFX_ChrInstance_PushMeshMatrixOverride` | 4 / 0 | named here [V] |
| 914 | `0x641E60` | `FFX_ChrInstance_SetBallParams` | 1 / 0 | named here [V] |
| 915 | `0x639240` | `FFX_Gfx_QueueFadeQuad1000` | 91 / 0 | named earlier |
| 916 | `0x7BD860` | `FFX_Came_StartTrapezoidMove` | 1 / 0 | named here [V] |
| 917 | `0x9DB170` | `FFX_MagicApi_GetCurrentEventId` | 1 / 0 | named here [V] |
| 918 | `0x6437E0` | `maybe_FFX_Vfx_SetDrawMdl2Param` | 1 / 0 | named here [I] |
| 919 | `0x1133398` | `g_ffxMagicSpecialActorList` | copied by 581 | named here [V] |
| 920 | `0x63A2B0` | `FFX_ChrInstance_GetBounds` | 0 | named here [V], never called |
| 921 | `0xC3BBB0` | `g_ffxPppOpTableForDlls` | copied by 581 | named earlier |
| 922 | `0x63C390` | `FFX_ChrInstance_SetHidden` | 10 / 0 | named earlier |
| 923 | `0x9DAFD0` | `MagicFile__setTexRemapFlag` | 1 / 0 | named here [V] |
| 924 | `0x63A410` | `maybe_FFX_ChrInstance_GetBoundsTransformed` | 2 / 0 | named here [I] |
| 925 | `0x794020` | `FFX_Battle_GetActor` | 4 / 0 | named earlier |
| 926 | `0x793650` | `FFX_Btl_IsAllyUnit` | 4 / 0 | named earlier |
| 927 | `0x6392C0` | `FFX_Gfx_QueueFadeQuad8000` | 1 / 0 | named earlier |
| 928 | `0x644DD0` | `FFX_Gfx_ResetDrawItemQueues` | 1 / 0 | named earlier |
| 929 | `0x63DAC0` | `FFX_Gfx_ClearModelMatrixOverrideList` | 2 / 0 | named earlier |
| 930 | `0x63D710` | `FFX_ChrInstance_SetClipPlane` | 2 / 0 | named here [V] |
| 931 | `0x644CA0` | `FFX_GfxCtx_PushPassCommand5UnlessCatchUp` | 16 / 0 | named earlier |
| 932 | `0x645840` | `FFX_GfxCtx_MarkDrawBatch` | 3 / 0 | named earlier |
| 933 | `0x643670` | `FFX_Gfx_SetMapColorScale` | 225 / 0 | named here [V] |
| 934 | `0x63A610` | `maybe_FFX_ChrInstance_GetBoundsWithJoints` | 2 / 0 | named here [I] |
| 935 | `0x1940AC4` | `g_ffxMagicHideChrOnStop` | copied by 581 | named here [V] |
| 936 | `0x12FB7D0` | `g_ffxIsCatchUpStep` | copied by 581 | named earlier |
| 937 | `0x713320` | `FFX_Vfx_ClearModelParts` | 4 / 0 | named here [V] |
| 938 | `0x644CF0` | `FFX_GfxCtx_PushPassCommand6UnlessCatchUp` | 2 / 0 | named earlier |
| 939 | `0x641100` | `FFX_GetMasterVersion` | 1 / 0 | named here [V] |
| 940 | `0xA44660` | `FFX_PrxPtr_SetLast` | 0 | named here [V], never called |
| 941 | `0xA44650` | `FFX_PrxPtr_Get` | 539 / 0 | named here [V] |
| 942 | `0xA44700` | `FFX_PrxPtr_Set` | 35 / 0 | named here [V] |
| 943 | `0xA446B0` | `FFX_PrxPtr_Add` | 0 | named here [V], never called |
| 944 | `0xA446A0` | `FFX_PrxPtr_IsNull` | 0 | named here [V], never called |
| 945 | `0x644C70` | `FFX_GfxCtx_PushCopyCommandUnlessCatchUp_01` | 2 / 0 | named earlier |
| 946 | `0x829C60` | `FFX_Vfx_ArmAlternateTexOnce` | 1 / 0 | named here [V] |
