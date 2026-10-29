"""
Apply the FFX_GraphicInitialize singleton names from reversing/BOOT_SEQUENCE.md section 6 to the
FFX.exe IDB.

FFX_GraphicInitialize 0x6419D0 builds 47 singletons in one pass. Sixteen of them had no name: steps
36, 37, 39, 43 to 47, 53, 54, 57 to 61 and 71 to 73. None has a vtable or RTTI. Five are named by
their own "X singleton is not created yet." strings (Distortion, MotionBlur, BrokenScreenEffect)
or by strings in their methods (ShadowManager_*, "CharacterLighting: %.2f fps",
RenderDelegationHelper::setDataToFreeList, the ld00..ld19 loading textures). The rest are named by
what reads them. Also here: the create, destroy and get functions of each, a few methods whose
role was read while identifying them, the getters of BlurEffect, BlurEffectEx and
PostProcessTweakUtil, and the globals of the already named singletons that still had dword_
names. Orphan copies with no code or data reference anywhere (rel32 and absolute scans) carry a
_dead suffix. Names starting maybe_ are guesses and say so.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all. A name starting maybe_ is a guess
and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_singleton_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
COMMENTS = False
VERBOSE = False

B = "BOOT_SEQUENCE.md 6"

# ---------------------------------------------------------------------------------------------
# Functions and code labels whose current name is a placeholder
# ---------------------------------------------------------------------------------------------

NAMES = {
    # step 36, a one byte object whose only live method is a no-op
    0x6E56A0: ("maybe_FFX_HeapFacade_CreateSingleton", "step 36: FFX_Alloc(1), byte 0 = 0, into maybe_g_ffxHeapFacade, guarded. The class is a guess: the code right after it (0x6E56F0, 0x6E5710, 0x6E5730 forward to FFX_MemAllocAligned, 0x6E5760 and 0x6E5780 to FFX_MemFree) has no reference anywhere, and the only live method is a no-op. " + B),
    0x6E56D0: ("maybe_FFX_HeapFacade_DestroySingleton", "step 36 teardown, from FFX__graphicFinalize. Frees and zeroes maybe_g_ffxHeapFacade. " + B),
    0x635A30: ("maybe_FFX_HeapFacade_Get_dead", "returns maybe_g_ffxHeapFacade. No code or data reference anywhere. " + B),
    0x6E5740: ("maybe_FFX_HeapFacade_OnFmvDynGeoChange", "retn 4, does nothing. The only live use of maybe_g_ffxHeapFacade: FFX_FmvMgr_ProcessCommands passes it as this with argument 0, right after it frees DynGeoMem set 0 (0x6D8E91) and right before it rebuilds it (0x6D8E2E). " + B),
    # step 37, DynGeoMem
    0x6DEB90: ("FFX_DynGeoMem_CreateSingleton", "step 37: FFX_Alloc(0x10) {+0 current set, +4 set 0, +8 set 1, +12 mutex}, FFX_DynGeoMem_BuildSet(0) builds set 0 (two 5 MB FFX_Malloc segments, dword_B4A650[0] = 0x500000) and selects it. Into g_ffxDynGeoMem. " + B),
    0x6DEC20: ("FFX_DynGeoMem_DestroySingleton", "step 37 teardown from FFX__graphicFinalize: frees set 0, closes the mutex, frees, zeroes g_ffxDynGeoMem. " + B),
    0x6383E0: ("FFX_DynGeoMem_Get_dead", "returns g_ffxDynGeoMem. No code or data reference anywhere, every reader loads the global directly. " + B),
    0x6DE720: ("FFX_DynGeoMem_BuildSet", "(this, set) frees the set, then builds it: a 0x18 byte segment list and two segments of dword_B4A650[set] bytes (0x500000 for both sets) from FFX_Malloc, each with its own mutex. From the create and from FFX_FmvMgr_ProcessCommands command 3. " + B),
    0x6DE860: ("FFX_DynGeoMem_FreeSet", "(this, set) frees both segments of the set and the set, and clears +0 when it was current. FFX_FmvMgr_ProcessCommands command 2 frees set 0 while a movie plays. " + B),
    0x6DEC90: ("FFX_DynGeoMem_SelectSet", "(this, set): +0 = set pointer. " + B),
    # step 39, the cluster cache
    0x69A060: ("FFX_ClusterCache_CreateSingleton", "step 39: FFX_Alloc(0x6420) + FFX_ClusterCache_ctor into g_ffxClusterCache, guarded. " + B),
    0x699AB0: ("FFX_ClusterCache_ctor", "+0 delayed release state = 2, +4 timer, +16 the path hash map, +24 a heap mutex handle, +24604 1 KB zeroed. The refcounted cache of every PCluster loaded by path (shaders, textures, fonts, character and map clusters). " + B),
    0x69A0E0: ("FFX_ClusterCache_DestroySingleton", "step 39 teardown from FFX__graphicFinalize. " + B),
    0x69A1A0: ("FFX_ClusterCache_Get", "returns g_ffxClusterCache, 138 call sites. Main thread, and the AsyncLoadManager worker through LoadClusterRequest__execute. " + B),
    0x69C010: ("FFX_ClusterCache_StepDelayRelease", "+4 += dt. State 1 releases after 3 s, state 0 logs 'Chr cache timingChrDelayRelease time out' after 12 s, both call FFX_ClusterCache_ApplyDelayedRelease. From FFX_ChrCache_StepDelayRelease. " + B),
    0x69A6B0: ("FFX_ClusterCache_ApplyDelayedRelease", "walks the cache map under its mutex and releases what was held back while release was delayed. From FFX_ClusterCache_StepDelayRelease and sub_640090. Name from the log line 'applyDelayedRelease to flush delay state' in sub_640090. " + B),
    # step 43, the post-process target manager
    0x692A90: ("FFX_PostProcessMgr_CreateSingleton", "step 43: FFX_Alloc(0x1240C) + FFX_PostProcessMgr_ctor into g_ffxPostProcessMgr, guarded. Also stored at gfx+4300. " + B),
    0x692020: ("FFX_PostProcessMgr_ctor", "two PClusters (+60, +74548), the scene targets (+140 colour, +144 depth, +156, +168, +172 per RENDER_PASSES.md 2), and the glow and fog values PostProcessTweakUtil__adjustSelected edits. " + B),
    0x692B90: ("FFX_PostProcessMgr_DestroySingleton", "step 43 teardown from FFX__graphicFinalize. " + B),
    0x692BE0: ("FFX_PostProcessMgr_Get", "returns g_ffxPostProcessMgr. The screen fade API (FFX_ScreenFade_*), the presenter and every post effect render call it. Main thread. " + B),
    0x692BF0: ("PostProcessTweakUtil__get", "returns g_postProcessTweakUtil (step 49). " + B),
    # step 44
    0x6ABE40: ("FFX_MaterialParamAnimator_CreateSingleton", "step 44: FFX_Alloc(0x28), ten zeroed dwords (four PArrays of {material, weight} rows and a time accumulator at +32), into g_ffxMaterialParamAnimator, guarded. " + B),
    0x6ABEB0: ("FFX_MaterialParamAnimator_DestroySingleton", "step 44 teardown from FFX__graphicFinalize. " + B),
    0x6ABF40: ("FFX_MaterialParamAnimator_Get", "returns g_ffxMaterialParamAnimator. " + B),
    0x6ABFC0: ("FFX_MaterialParamAnimator_SetWeights", "(this, materialId, &w, &w2) writes the weight into every row of the four arrays whose material +88 matches, w2 into the fourth array's second slot. Step time, from FFX_Map_StepAnims through sub_914A10 and sub_6436E0. " + B),
    0x6ACD20: ("FFX_MaterialParamAnimator_ApplyAll", "(this, dt) +32 += dt, then pushes every row to its material as MorphWeight, UVMorphWeight, UvOffsetX/Y, VertexColorMorphWeight. Per present, from FFX_Frame_UpdateBoostersAndOverlays. VISUAL_CENSUS.md. " + B),
    # step 45
    0x6AD0D0: ("RenderDelegationHelper_ctor", "step 45, called inline by FFX_GraphicInitialize on FFX_Alloc(0x208): two words zeroed, then RenderDelegationHelper_InitPool. DISPLAY_LIST.md 6. " + B),
    0x6ADC90: ("RenderDelegationHelper_InitPool", "a 16 entry PArray and 64 pool slots, each a 20 byte record from FFX_Alloc. DISPLAY_LIST.md 6. " + B),
    0x6357F0: ("RenderDelegationHelper_CreateSingleton_dead", "the guarded create of g_renderDelegationHelper that FFX_GraphicInitialize inlines. No code or data reference anywhere. " + B),
    0x635A20: ("RenderDelegationHelper_Get_dead", "returns g_renderDelegationHelper. No code or data reference anywhere. " + B),
    # step 47
    0x6A6D50: ("CharacterLightingTweakUtil__createSingleton", "step 47: FFX_Alloc(0xFC) + ctor into g_characterLightingTweakUtil, guarded. The DebugMenuManager mode 0 page, sibling of PostProcessTweakUtil. " + B),
    0x6A6610: ("CharacterLightingTweakUtil__ctor", "+0 labels ready = 0, the debug text block at +4, word +200 = 0x100 (0x6A6640) so +200 shown = 0 and +201 = 1, +204..+216 four light intensities = 1.0, +224/+228 = 1280x720, +248 the mesh row table (allocated by initLabels). " + B),
    0x6A6E00: ("CharacterLightingTweakUtil__destroySingleton", "step 47 teardown from FFX__graphicFinalize. " + B),
    0x6A6E40: ("CharacterLightingTweakUtil__get", "returns g_characterLightingTweakUtil. " + B),
    0x6A91E0: ("CharacterLightingTweakUtil__initLabels", "+248 = FFX_Malloc(0x4B0), 100 rows of 12 bytes. FFX_DebugText_LoadFont always returns 0 (0x6F63AB), so +0 = 1 at 0x6A93AB on every retail boot (from sub_657A50 in FFX_Boot_FirstAsyncLoad), and the 11 labels 'Light index' .. 'Auto reset intensity' are set. " + B),
    0x6AA5F0: ("CharacterLightingTweakUtil__setEnabled", "+200 = enable && +0, shows or hides the debug text. Only DebugMenuManager calls it. " + B),
    0x6A9F00: ("CharacterLightingTweakUtil__draw", "'CharacterLighting: %.2f fps'. From FFX_Frame_RenderScene 0x66AF34. " + B),
    0x6AA3D0: ("CharacterLightingTweakUtil__resize", "+224/+228 = size, from FFX_Gfx_ResizeAllTargets. " + B),
    0x6AB5C0: ("CharacterLightingTweakUtil__releaseLabels", "frees the debug text and the +248 table. " + B),
    # step 53
    0x6C1420: ("FFX_VfxShaderTable_CreateSingleton", "step 53: FFX_Alloc(0x9C) + ctor into g_ffxVfxShaderTable, guarded. " + B),
    0x6BF4A0: ("FFX_VfxShaderTable_ctor", "three PMaps (+0, +48, +104 heads), +96 and +152 bytes 0, +100 = -1. Filled by FFX_Vfx_LoadShaderManifest, read by FFX_VfxShaderTable_Find. " + B),
    0x6C14A0: ("FFX_VfxShaderTable_DestroySingleton", "step 53 teardown from FFX__graphicFinalize. Frees but does not zero the global. " + B),
    0x6C14C0: ("FFX_VfxShaderTable_Get", "returns g_ffxVfxShaderTable. " + B),
    # step 54
    0x6C2190: ("Distortion__createSingleton", "step 54: FFX_Alloc(0x80) + Distortion__ctor into g_ffxDistortion, guarded. " + B),
    0x6C2010: ("Distortion__ctor", "the screen wave grid owner. +120 strength 0.75, +124 on, +125 skip, +126 rearm. DYNAMIC_MESHES.md 4, VISUAL_CENSUS.md. " + B),
    0x6C2210: ("Distortion__destroySingleton", "step 54 teardown from FFX__graphicFinalize. " + B),
    # step 57
    0x6E76C0: ("FFX_ButtonPrompt_CreateSingleton", "step 57: FFX_Alloc(0x168) + ctor into g_ffxButtonPrompt, guarded. " + B),
    0x6E6330: ("FFX_ButtonPrompt_ctor", "two 0xB0 and 0xA0 byte tables set to -1, the key and pad cell maps (sub_6E6730, sub_6E6890), +352/+356 scale 1.0. The PC prompt substitution that FFX_ButtonPrompt_Substitute runs from FFX_Sprite2D_Draw. " + B),
    0x6E76F0: ("FFX_ButtonPrompt_DestroySingleton", "step 57 teardown from FFX__graphicFinalize. " + B),
    0x6E7710: ("FFX_ButtonPrompt_Get", "returns g_ffxButtonPrompt. " + B),
    0x6E7720: ("FFX_ButtonPrompt__setScale", "+352 = x, +356 = y. " + B),
    0x642EC0: ("FFX_ButtonPrompt_SetScale", "FFX_ButtonPrompt__setScale on the singleton. From FFX_MesWin_ApplyControlCode and FFX_BtlOd_DrawAuronSeq. " + B),
    0x642C60: ("FFX_ButtonPrompt_ResetScale", "scale back to 1.0, 1.0. From FFX_MesWin_ApplyControlCode, FFX_BtlOd_DrawAuronSeq, sub_9029B0. " + B),
    # step 58
    0x6C5030: ("MotionBlur__createSingleton", "step 58: FFX_Alloc(0x8C) + MotionBlur__ctor into g_ffxMotionBlur, guarded. " + B),
    0x6C4EC0: ("MotionBlur__ctor", "a PCluster at +0, +92 = 0.27, +88/+89 enable and skip-one. BlendFrame.cgfx. " + B),
    0x6C50B0: ("MotionBlur__destroySingleton", "step 58 teardown from FFX__graphicFinalize. " + B),
    0x6C5130: ("MotionBlur__getSingleton", "'MotionBlur singleton is not created yet.' " + B),
    # step 59
    0x6C5790: ("FFX_SpecularPass_CreateSingleton", "step 59: FFX_Alloc(0x5B4) + ctor into g_ffxSpecularPass, guarded. " + B),
    0x6C5670: ("FFX_SpecularPass_ctor", "four zeroed dwords, then four 356 byte slots at +16 and four bytes at +1456. Pass command 4, the specular character pass. " + B),
    0x6C57C0: ("FFX_SpecularPass_DestroySingleton", "step 59 teardown. Frees but does not zero the global. " + B),
    0x6C57E0: ("FFX_SpecularPass_Get", "returns g_ffxSpecularPass. From the FFX_Gfx_QueueSpecularPass* setters, FFX_GfxCtx_ExecPassCommand and FFX_Frame_RenderScene. " + B),
    # step 60
    0x6C9500: ("ShadowManager__createSingleton", "step 60: FFX_Alloc(0x4068) + ShadowManager__ctor into g_shadowManager, guarded. Nothing frees it. " + B),
    0x6C7D80: ("ShadowManager__ctor", "a PCluster at +4, two lock pairs at +656 and +672, +16480 = 3. The character shadow atlas, render targets ShadowManager_* (sub_6D18B0). " + B),
    0x6D2390: ("ShadowManager__get", "returns g_shadowManager. 43 call sites. " + B),
    0x6CC980: ("ShadowManager__getSpareGradMeshInstance", "from its own error string 'GetASpareGradMeshInstance() , Shouldn't get here'. DYNAMIC_MESHES.md 7. " + B),
    # step 61
    0x6D4D30: ("BrokenScreenEffect__createSingleton", "step 61: FFX_Alloc(0x78) + ctor into g_brokenScreenEffect, guarded. " + B),
    0x6D4BA0: ("BrokenScreenEffect__ctor", "+8 alpha 1.0, +6 = 1, +108/+112 = 1280x720, a PCluster for the shards. DYNAMIC_MESHES.md 6. " + B),
    0x6D4DB0: ("BrokenScreenEffect__destroySingleton", "step 61 teardown from FFX__graphicFinalize. " + B),
    0x6D4DE0: ("BrokenScreenEffect__getSingleton", "'BrokenScreenEffect singleton is not created yet.' " + B),
    # step 71
    0x6E3240: ("FFX_FogMgr_CreateSingleton", "step 71: FFX_Alloc(0xB8), ctor inline (+160..+176 fog fields, +172 = 1.0, +180 = 2) into g_ffxFogMgr, guarded. " + B),
    0x6E32A0: ("FFX_FogMgr_DestroySingleton", "step 71 teardown. Frees but does not zero the global. " + B),
    0x6E32C0: ("FFX_FogMgr__get", "returns g_ffxFogMgr. From FFX_Fog_Set* (MapFx, step) and the fog install (render). TEXTURE_ANIMATION_AND_POST_TWEAKS.md. " + B),
    # step 72
    0x6E45E0: ("FFX_LoadingIndicator_ctor", "step 72: +0 drawing now, +1 requested, +4 spinner clock, +8 delay timer, +16 state 0..3, +20 alpha, +24/+28 the 20 ld00..ld19 clusters, +40 loadingbg, +52 a PCluster. FFX_GraphicInitialize allocates it with no guard (0x641AEE). " + B),
    0x6E4770: ("FFX_LoadingIndicator_LoadTextures", "loads menu/loading ld00..ld19 (off_C34F70) and loadingbg through the cluster cache. From sub_6656E0 at boot. " + B),
    0x6E4EA0: ("FFX_LoadingIndicator_Draw", "draws the spinner mesh when shown or state >= 2. From FFX_Boot_PresentFrame and FFX_Frame_RenderScene. " + B),
    0x6E4FF0: ("FFX_LoadingIndicator_SetParams", "(this, &clock, &state, &alpha), each optional: +4, +16, +20. FFX_BootSequence passes (3.0, null, 0.0) at 0x64175E. " + B),
    0x6E4FD0: ("FFX_LoadingIndicator_Reset", "+16 state, +4, +8, +20 = 0. " + B),
    0x643500: ("FFX_LoadingIndicator_SetVisible", "(on, mode): shows (app+928 = 1, +1 = 1) or hides (+1 = 0) the loading indicator. ATEL Core 607. " + B),
    0x635780: ("FFX_LoadingIndicator_CreateSingleton_dead", "a guarded create of g_ffxLoadingIndicator. No code or data reference anywhere, FFX_GraphicInitialize allocates unguarded instead. " + B),
    0x635A10: ("FFX_LoadingIndicator_Get_dead", "returns g_ffxLoadingIndicator. No code or data reference anywhere. " + B),
    # step 73
    0x6E5490: ("FFX_FrameTimeMonitor_CreateSingleton", "step 73: FFX_Alloc(1) into g_ffxFrameTimeMonitor, guarded, no ctor. The class's sampling code (0x6E54C0, 0x6E55B0, 0x6E55F0, 0x6E5610) has no reference anywhere. " + B),
    0x6E55D0: ("FFX_FrameTimeMonitor_Get", "returns g_ffxFrameTimeMonitor. One caller, FFX_Boot_FirstAsyncLoad 0x6364D9. " + B),
    0x6E55E0: ("FFX_FrameTimeMonitor_Init_nop", "retn. Called on g_ffxFrameTimeMonitor by FFX_Boot_FirstAsyncLoad 0x6364E0. " + B),
    0x6E54B0: ("FFX_FrameTimeMonitor_Destroy_nop", "retn. Called by FFX__graphicFinalize where the destroy would be, the byte is never freed. " + B),
    0x6E53C0: ("FFX_FrameTimeMonitor_SampleCallback_dead", "a renderer callback that averages frame times in ms (x1000) and shifts the 3 sample history flt_CE5188..CE5190. Only the orphan 0x6E54C0 queues it. The verdict dword_CE5198 (-1 above 26 and 40 ms, 1 under 14 ms, else 0) is written there and read nowhere. " + B),
    0x6E5690: ("FFX_FrameTimeMonitor_ResetCallback_dead", "dword_CE59A0 = 0. Only the orphan 0x6E5610 queues it. " + B),
    # getters of named singletons
    0x6C3580: ("BlurEffect__getSingleton", "'BlurEffect singleton is not created yet.' Returns g_blurEffect (step 55). " + B),
    0x6C4A70: ("BlurEffectEx__getSingleton", "'BlurEffectEx: singleton is not created yet.' Returns g_blurEffectEx (step 56). " + B),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xCE63C4: ("maybe_g_ffxHeapFacade", "step 36, a 1 byte object. Read only by FFX_FmvMgr_ProcessCommands as this for a no-op. The class is a guess. " + B),
    0xCDEDF0: ("g_ffxDynGeoMem", "step 37, the DynGeoMem manager, 0x10 bytes {+0 current set, +4 set 0, +8 set 1, +12 mutex}, each set two 5 MB segments. Per step vertex scratch for Sprite2D, Glyph2D and VFX model parts, flipped per present. " + B),
    0xCCCA4C: ("g_ffxClusterCache", "step 39, 0x6420 bytes, the refcounted path keyed cache of loaded PClusters with a delayed release timer. Main thread and the AsyncLoadManager worker, under its mutex. " + B),
    0xCCC8A0: ("g_ffxPostProcessMgr", "step 43, 0x1240C bytes, the post-process target manager (also gfx+4300): scene targets, fade state, glow and fog. RENDER_PASSES.md 2, SCREENS_AND_FADES.md. " + B),
    0xCCCA90: ("g_ffxMaterialParamAnimator", "step 44, 0x28 bytes, the material morph and UV weight rows. Written at step, applied per present. " + B),
    0xCCCA98: ("g_renderDelegationHelper", "step 45, 0x208 bytes, RenderDelegationHelper, the phase masked callback registry. DISPLAY_LIST.md 6. " + B),
    0xCCCA88: ("g_characterLightingTweakUtil", "step 47, 0xFC bytes, the CharacterLighting debug page (DebugMenuManager mode 0). +0 and +201 are 1 on every retail boot, so the 100 row mesh table at +248 is kept by FFX_ChrLight_(Un)RegisterMeshInstance and the four intensities at +204 mirror g_ffxChrLight (FFX_Gfx_ApplyCharacterLights writes both). The page itself never opens. " + B),
    0xCDEB80: ("g_ffxVfxShaderTable", "step 53, 0x9C bytes, the VFX shader lookup filled by FFX_Vfx_LoadShaderManifest. " + B),
    0xCDEB88: ("g_ffxDistortion", "step 54, 0x80 bytes, Distortion, the screen wave grid. " + B),
    0xCE6DC0: ("g_ffxButtonPrompt", "step 57, 0x168 bytes, the PC button prompt substitution table. " + B),
    0xCDEC04: ("g_ffxMotionBlur", "step 58, 0x8C bytes, MotionBlur. " + B),
    0xCDEC1C: ("g_ffxSpecularPass", "step 59, 0x5B4 bytes, the specular character pass slots. " + B),
    0xCDEC28: ("g_shadowManager", "step 60, 0x4068 bytes, ShadowManager, the character shadow atlas. Never freed. " + B),
    0xCDEC98: ("g_brokenScreenEffect", "step 61, 0x78 bytes, BrokenScreenEffect, the glass shard effect. " + B),
    0xCE5170: ("g_ffxFogMgr", "step 71, 0xB8 bytes, the fog manager. " + B),
    0xCE517C: ("g_ffxLoadingIndicator", "step 72, 0x44 bytes, the loading spinner (ld00..ld19, loadingbg). " + B),
    0xCE5194: ("g_ffxFrameTimeMonitor", "step 73, a 1 byte object whose only live call is a no-op. Inert in retail. " + B),
    0xCDEDE4: ("g_jobSchedule", "step 34, JobSchedule, 0x238 bytes, the 8 slot job ring the four JobWorkers serve. " + B),
    0xCCCA6C: ("g_ffxPoolSet", "step 42, the data, vfxdg, segment and name pools. " + B),
    0xCCCAA8: ("g_textureAnimationManager", "step 52, TextureAnimationManager. " + B),
    0xCDEBA0: ("g_blurEffect", "step 55, BlurEffect. " + B),
    0xCDEBFC: ("g_blurEffectEx", "step 56, BlurEffectEx. " + B),
    0xCCC89C: ("g_postProcessTweakUtil", "step 49, PostProcessTweakUtil, the PostProcess debug page. " + B),
}

PLACEHOLDER = re.compile(
    r"^(sub|nullsub|loc|j_sub|unk|dword|word|byte|qword|off|flt|dbl|stru|funcs)_[0-9A-Fa-f]+$"
    r"|^[A-Za-z]+__f[0-9A-Fa-f]{6}$"
    r"|^FFX_AtelSys_[A-Za-z]+_\d+_(start|poll|resf|resi)$")


def _cur_name(ea):
    import idc
    return idc.get_name(ea) or ""


def _plan(table, kind):
    import idaapi
    import ida_name
    plan, skips, kept = [], [], []
    for ea, (new, why) in sorted(table.items()):
        cur = _cur_name(ea)
        if cur == new:
            kept.append("0x%X %s" % (ea, new))
            continue
        m = re.match(r"^was ([^ ]+)\.", why)
        if kind == "RENAMES" or (kind == "GLOBALS" and m):
            if not m:
                skips.append("0x%X %s: comment does not start 'was OLD.'" % (ea, new))
                continue
            if cur != m.group(1):
                skips.append("0x%X is %s, expected %s" % (ea, cur or "(no name)", m.group(1)))
                continue
        else:
            if cur and not PLACEHOLDER.match(cur):
                skips.append("0x%X is %s, not a placeholder" % (ea, cur))
                continue
        other = ida_name.get_name_ea(idaapi.BADADDR, new)
        if other != idaapi.BADADDR and other != ea:
            skips.append("0x%X name %s already used at 0x%X" % (ea, new, other))
            continue
        plan.append((ea, cur, new, why))
    return plan, skips, kept


def _add_comment(ea, text):
    import idc
    import ida_funcs
    if not text:
        return False
    f = ida_funcs.get_func(ea)
    if f and f.start_ea == ea:
        old = idc.get_func_cmt(ea, 1) or ""
        if text in old or len(old) + len(text) + 1 > 1000:
            return False
        return bool(idc.set_func_cmt(ea, (old + "\n" + text) if old else text, 1))
    old = idc.get_cmt(ea, 1) or ""
    if text in old or len(old) + len(text) + 1 > 1000:
        return False
    return bool(idc.set_cmt(ea, (old + " " + text) if old else text, 1))


def main(dry_run=DRY_RUN, comments=COMMENTS, verbose=VERBOSE):
    import idc
    import ida_name
    total = 0
    failed = 0
    for kind, table in (("NAMES", NAMES), ("RENAMES", RENAMES), ("GLOBALS", GLOBALS)):
        plan, skips, kept = _plan(table, kind)
        for line in kept:
            if verbose:
                print("ok    %s %s" % (kind, line))
        for line in skips:
            print("skip  %s %s" % (kind, line))
        for ea, cur, new, why in plan:
            print("name  %s 0x%X %s -> %s%s" % (kind, ea, cur or "(no name)", new, ("    " + why) if verbose else ""))
        print("%s: %d to rename, %d skipped, %d already named, %d rows" % (kind, len(plan), len(skips), len(kept), len(table)))
        total += len(plan)
        if dry_run:
            continue
        for ea, cur, new, why in plan:
            if not idc.set_name(ea, new, ida_name.SN_NOWARN | ida_name.SN_NOCHECK):
                print("FAILED 0x%X %s -> %s" % (ea, cur, new))
                failed += 1
            elif comments:
                _add_comment(ea, why)
    if dry_run:
        print("DRY_RUN, nothing renamed (%d would be)" % total)
        return True
    print("renamed %d, failed %d. The database is NOT saved, save it when you are happy" % (total - failed, failed))
    return failed == 0


def headless():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True, help="a COPY of the database")
    ap.add_argument("--apply", action="store_true", help="rehearse the renames, the database is not saved")
    ap.add_argument("--comments", action="store_true", help="also append each row's comment")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    import idapro
    if idapro.open_database(a.idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % a.idb)
    try:
        main(dry_run=not a.apply, comments=a.comments, verbose=a.verbose)
    finally:
        idapro.close_database(save=False)


if __name__ == "__main__":
    if "idaapi" in sys.modules or "ida_kernwin" in sys.modules:
        main()
    else:
        headless()
