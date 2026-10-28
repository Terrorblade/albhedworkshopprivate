"""
Apply the rcBg background renderer names from reversing/RCBG.md to the FFX.exe IDB.

rcBg is the pre-rendered background library: rcbgMain, rcbgPre, rcbgScene, rcbgLoad, rcbgModel,
rcbgTrx, rcbgTrxTex, rcbgAnime, rcbgGuide (the guide map), rcbgFP (the per-map field particle
objects), plus the FFX side glue at 0x9199F0..0x91AF2F that the MapFx ATEL library and the main
loop call. The files are laid out in makefile order, not alphabetically (RCBG.md 2). The exact
names that survive are kept. rcbgPSim and rcFlex are in apply_rcflex_names.py and are not repeated
here. rcbgDebug, rcbgSave, rcbgSaveText and rcbgLoadText are only reachable from the orphaned rcui
debug editor, so only their string anchors are named.

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

    python apply_rcbg_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
COMMENTS = False
VERBOSE = False

# ---------------------------------------------------------------------------------------------
# Functions and code labels whose current name is a placeholder
# ---------------------------------------------------------------------------------------------

NAMES = {
    # FFX side glue, 0x9199F0..0x91AF2F. Every getter and setter goes through g_ffxRcBgCtx +0 (the
    # scene set) +8 (the current scene), layers at scene +8 with a 192 byte stride
    0x919A10: ("FFX_RcBg_GetFPGroup90Work", "FFX_RcBgFP_GetGroupOpWork(90, 6, 2), the work of map effect group 90 op 6 that the FP objects and the getters at 0x91ADF0 read"),
    0x919A20: ("FFX_RcBg_SaveGuideScale", "saves the guide map scale (guide +108) to flt_193BF7C through rcbgGuideSaveScale"),
    0x919A40: ("FFX_RcBg_RestoreGuideScale", "puts the saved guide map scale back through rcbgGuideRestoreScale"),
    0x919A60: ("FFX_RcBg_Teardown", "rcbgMainTeardown, from FFX_Map_UnloadMapOut. Frees the context and clears the scene camera override"),
    0x919A80: ("FFX_RcBg_LinkFPObjChrs", "mapfx:103. Finds the rcBg object of type 0x20008 (the FP object) and hands it two actor CHRs and a mode (rcbgFPBltzLinkChrs). Blitzball only"),
    0x919AB0: ("FFX_RcBg_StopFPObj", "mapfx:104. rcbgFPBltzStop on the 0x20008 object"),
    0x919BC0: ("FFX_RcBg_GetParticleTextureCount", "frame count of a section 12 particle texture (rcbgTrxTexGetCount). From the ppp sea and camera operators"),
    0x919BD0: ("FFX_RcBg_SetParticleTextureFrame", "rcbgTrxTexSetFrame on a section 12 particle texture"),
    0x919BE0: ("FFX_RcBg_SetParticleTextureTex0", "rcbgTrxTexSetTex0 on a section 12 particle texture"),
    0x919C70: ("FFX_RcBg_GetGuideModelIndex", "current scene +26, the section 11 model index the guide map uses"),
    0x919CA0: ("FFX_RcBg_GetGuideScale", "mapfx:51, rcbgGuideGetScale"),
    0x919D00: ("FFX_RcBg_DrawGuideMap", "from FFX_Field_DrawGuideMap 0x872CA0, rcbgGuideDraw. Step thread, after the sub-steps. RCBG.md 4.4"),
    0x919D70: ("FFX_RcBg_SetGuideMapEnabled", "rcbgGuideSetEnabled: bit 0 of g_rcbgGuideFlags. Callers 0x861510, 0x8655E0 (FFX_MenuSys_StepFrame), 0x865DC0"),
    0x919DF0: ("FFX_RcBg_SetGuideScale", "mapfx:50, rcbgGuideSetScale (guide +108)"),
    0x919E20: ("FFX_RcBg_SetGuideMapLayout", "core:243 and FFX_Ev_LoadEventPackage. rcbgGuideSetLayout: preset 0..4 picks the corner and size"),
    0x919E50: ("FFX_RcBg_SetGuideMapPos", "core:325. rcbgGuideSetPos, the guide map screen centre"),
    0x919E80: ("FFX_RcBg_SetGuideMapSize", "core:326. rcbgGuideSetSize"),
    0x919F40: ("FFX_RcBg_ClearLayerAnimFlag2", "mapfx:33. Layer anime flags (layer +144) &= ~2"),
    0x919F90: ("FFX_RcBg_SetLayerAnimFlag2", "mapfx:32. Layer anime flags |= 2"),
    0x919FE0: ("FFX_RcBg_SetLayerAnimMode", "mapfx:34. Replaces bits 8..11 of the layer anime flags, the loop mode rcbgAnimeStep switches on"),
    0x91A030: ("FFX_RcBg_ClearLayerAnimFlag1", "mapfx:31. Layer anime flags &= ~1"),
    0x91A080: ("FFX_RcBg_SetLayerAnimFlag1", "mapfx:30. Layer anime flags |= 1"),
    0x91A0F0: ("FFX_RcBg_TweenLayerPairB", "mapfx:85. Starts the layer +76/+80 tween (from +100..+112, frames +124, counter +128) that rcbgLayerTween steps"),
    0x91A150: ("FFX_RcBg_TweenLayerScale", "mapfx:79. Starts the layer scale tween +68/+72 (from +84..+96, frames +116, counter +120)"),
    0x91A200: ("FFX_RcBg_ClearSceneFlag0", "mapfx:45. Scene +0 bit 0 off, then sub_65F0E0"),
    0x91A220: ("FFX_RcBg_SetSceneFlag0", "mapfx:44. Scene +0 bit 0 on, then FFX_Gfx_RelinkMapMeshInstances"),
    0x91A260: ("FFX_RcBg_SetSceneField260", "mapfx:106. Scene +260"),
    0x91A2A0: ("FFX_RcBg_GetLayerFrameCount", "mapfx:28. Layer +152, the anime frame count"),
    0x91A2E0: ("FFX_RcBg_GetLayerByte67", "mapfx:49. Layer byte +67"),
    0x91A330: ("FFX_RcBg_GetLayerPairBX", "layer +76"),
    0x91A380: ("FFX_RcBg_GetLayerPairBY", "layer +80"),
    0x91A3D0: ("FFX_RcBg_GetLayerCount", "current scene +4. Every layer getter and setter bounds checks against it"),
    0x91A3F0: ("FFX_RcBg_GetLayerOriginY", "mapfx:27. Layer +8"),
    0x91A440: ("FFX_RcBg_GetLayerX", "mapfx:18. Layer +12, the X the rcbgSaveText writer prints"),
    0x91A490: ("FFX_RcBg_GetLayerY", "mapfx:19. Layer +16"),
    0x91A4E0: ("FFX_RcBg_GetLayerScaleY", "mapfx:81. Layer float +72"),
    0x91A530: ("FFX_RcBg_GetLayerScaleX", "mapfx:80. Layer float +68"),
    0x91A580: ("FFX_RcBg_GetLayerField36", "mapfx:23. Layer +36"),
    0x91A5D0: ("FFX_RcBg_GetLayerField40", "mapfx:24. Layer +40"),
    0x91A620: ("FFX_RcBg_GetLayerOriginX", "mapfx:26. Layer +4"),
    0x91A670: ("FFX_RcBg_GetLayerField20", "mapfx:21. Layer float +20, the first of the four Z floats rcbgSaveText prints"),
    0x91A6D0: ("FFX_RcBg_GetSceneRoll", "mapfx:41. Scene +172"),
    0x91A700: ("FFX_RcBg_GetSceneShakeX", "mapfx:36. Scene +184"),
    0x91A730: ("FFX_RcBg_GetSceneShakeY", "mapfx:37. Scene +188"),
    0x91A770: ("FFX_RcBg_GetLayerFlags", "layer +0. From FFX_DreamFilter (0x822580, 0x822620)"),
    0x91A7C0: ("FFX_RcBg_HideLayer", "mapfx:16. Layer +0 |= 1, then sub_638F30 and sub_644120(i, 0). Events 347 and 212 widen the bound to 28 layers"),
    0x91A830: ("FFX_RcBg_ShowLayer", "mapfx:15. Layer +0 &= ~1, then sub_638F60 and sub_644120(i, 1). In te and test20 it also raises g_ffxTitleScreenActive"),
    0x91A8D0: ("FFX_RcBg_ResetSceneRoll", "mapfx:43. rcbgSceneResetRoll"),
    0x91A900: ("FFX_RcBg_ResetSceneShake", "mapfx:39. rcbgSceneResetShake"),
    0x91A930: ("FFX_RcBg_SetLayerFrame", "mapfx:29 and mapfx:93. Anime frame word at layer +150"),
    0x91A970: ("FFX_RcBg_SetLayerLastFrame", "mapfx:47. +150 = +152 - 1"),
    0x91A9C0: ("FFX_RcBg_SetLayerAnimSpeed", "mapfx:46. Layer +164, the anime speed (anime +20, 256 at init)"),
    0x91AA00: ("FFX_RcBg_StartSceneRoll", "mapfx:42. Scene +220 mode, +224 speed, +228 frames, +232 amplitude, phase +236 and counter +240 = 0. rcbgSceneStepRoll runs it"),
    0x91AA90: ("FFX_RcBg_SetLayerByte67", "mapfx:48. sub_639D20 and the byte at layer +67"),
    0x91AAE0: ("FFX_RcBg_SetLayerPairB", "mapfx:82..84. Layer +76/+80, stops their tween"),
    0x91AB30: ("FFX_RcBg_SetLayerPos", "mapfx:17. Moves the mesh by the delta through sub_639D40, then layer +12/+16. Scene 382 layer 26 has a one pixel fix up"),
    0x91ABC0: ("FFX_RcBg_SetLayerScale", "mapfx:76..78. Layer +68/+72 (0 keeps the old one), stops the tween, pushes sub_639D60(i, +4, +8, sx, sy)"),
    0x91AC40: ("FFX_RcBg_SetLayerFields36And40", "mapfx:22. Layer +36 and +40"),
    0x91AC80: ("FFX_RcBg_SetLayerOrigin", "mapfx:25. Layer +4/+8, the pivot rcbgLayerTween and SetLayerScale pass to sub_639D60"),
    0x91ACC0: ("FFX_RcBg_SetLayerField20", "mapfx:20. Layer float +20"),
    0x91AD10: ("FFX_RcBg_SetSceneRoll", "mapfx:40. Scene +172, then FFX_RcBg_ApplySceneCamera"),
    0x91AD40: ("FFX_RcBg_SetSceneShakeOffset", "mapfx:35. Scene +184/+188, then FFX_RcBg_ApplySceneCamera"),
    0x91AD80: ("FFX_RcBg_StartSceneShake", "mapfx:38. Scene +192 mode, +196 speed, +200 frames, +204 amplitude, +208 random range, phase +212 and counter +216 = 0. rcbgSceneStepShake runs it, and draws CRT rand when +208 > 0"),
    0x91ADF0: ("FFX_RcBg_GetFPWorkPtr80", "&group 90 work +80"),
    0x91AE00: ("FFX_RcBg_GetFPMatrix", "returns &dword_193BF28, a 64 byte matrix the battle special code and FP objects share"),
    0x91AE10: ("FFX_RcBg_GetFPWorkPtr64", "&group 90 work +64"),
    0x91AE30: ("FFX_RcBg_ResetFPMatrix", "dword_193BF28 = identity"),
    0x91AE40: ("FFX_RcBg_CopyFPWork80", "copies group 90 work +80 out"),
    0x91AE60: ("FFX_RcBg_SetFPMatrix", "copies a matrix into dword_193BF28"),
    0x91AE80: ("FFX_RcBg_CopyFPWork64", "copies group 90 work +64 out"),
    0x91AEA0: ("FFX_RcBg_SetFPWorkActive", "group 90 work +228 = 1 and +96/+100"),
    0x91AF10: ("FFX_RcBg_GetTrxSize", "s16 pair at trx +16/+18. From pppDrawMdlSea_Draw"),
    0x91C8A0: ("FFX_MapFx_IsEvent320", "g_ffxCurrentEventId == 320. The fog rows mapfx:55 and 62..65 drive the renderer directly in that event"),

    # rcbgGuide.c, the guide map (minimap) 0x91CAE0..0x91F64D
    0x91CAE0: ("rcbgGuide_VecScale", "rcbgGuide.c vector helper"),
    0x91CB40: ("rcbgGuide_VecCopy", "rcbgGuide.c vector helper"),
    0x91CB70: ("rcbgGuide_VecScale2", "rcbgGuide.c vector helper through sub_7500D0"),
    0x91CBD0: ("rcbgGuide_VecCopy2", "rcbgGuide.c vector helper"),
    0x91CC00: ("rcbgGuide_ClosestPointOnSegment", "2D closest point on a segment, for the marker arrows"),
    0x91CCC0: ("rcbgGuideSaveScale", "guide +108 -> flt_193BF7C"),
    0x91CCE0: ("rcbgGuideRestoreScale", "flt_193BF7C -> guide +108"),
    0x91CD00: ("rcbgGuide_SegmentIntersect", "2D segment intersection, for the marker arrows"),
    0x91CEF0: ("rcbgGuideDelete", "Final then free"),
    0x91CF10: ("rcbgGuideDrawArrow", "draws a marker arrow toward a target on the minimap edge"),
    0x91D280: ("rcbgGuideFadeStep", "(show). State g_rcbgGuideFadeState 0 hidden, 1 fading in, 2 shown, 3 fading out. Delay g_rcbgGuideFadeDelayFrames before each fade, level 0..g_rcbgGuideFadeFrames, alpha = level * 128 / frames into g_rcbgGuideAlpha"),
    0x91D3D0: ("rcbgGuideFinal", "destroys the fog dyngeo group keyed by the guide, deletes the model built from the walkmesh (+8)"),
    0x91D420: ("rcbgGuideClearFlags", "g_rcbgGuideFlags &= ~mask"),
    0x91D430: ("rcbgGuideSetFlags", "g_rcbgGuideFlags |= mask"),
    0x91D440: ("rcbgGuideGetFlags", "g_rcbgGuideFlags, uncalled"),
    0x91D450: ("rcbgGuideGetScale", "guide +108"),
    0x91D460: ("rcbgGuideGetModel", "the section 11 YNGM model at guide +0, or the one built from the walkmesh at guide +8"),
    0x91D480: ("rcbgGuideGetLayout", "g_rcbgGuideLayout"),
    0x91D490: ("rcbgGuideInit", "120 byte guide, +108 scale = 350, g_rcbgGuideFlags |= 0x100, rcbgGuideFadeReset"),
    0x91D4E0: ("rcbgGuideNew", "allocates and inits the guide, from rcbgMainInit"),
    0x91D510: ("rcbgGuideSetFlag8", "bit 3 (8) of g_rcbgGuideFlags on or off"),
    0x91D560: ("rcbgGuideSetEnabled", "bit 0 of g_rcbgGuideFlags on or off. rcbgGuideDraw returns when it is clear (0x91DD5D)"),
    0x91D580: ("rcbgGuideSetScale", "guide +108"),
    0x91D590: ("rcbgGuideSetModel", "a model, or 0 to build one from the active walkmesh (rcbgGuideBuildModel)"),
    0x91D5E0: ("rcbgGuideBuildModel", "builds an rcbgModel from the FFX_Map_GetActiveWalkmesh polygons, at most 512, else 'Guide Map Error:ID MAP no POLYGON oosugi desu!!'"),
    0x91D880: ("rcbgGuideSetup", "model +64, alpha 128, bbox -> +80, extents +96..+104, then FFX_GuideMap_LoadSceneRecord"),
    0x91D900: ("rcbgGuideSetPos", "g_rcbgGuidePosX/Y, the screen centre"),
    0x91D920: ("rcbgGuideSetSize", "g_rcbgGuideSize"),
    0x91D930: ("rcbgGuideSetLayout", "preset 0..3 put a size 128 map at (80,64), (432,64), (80,352), (432,352). Preset 4 is (256,208) at size 256. Writes g_rcbgGuideLayout, Size, PosX, PosY"),
    0x91DA10: ("rcbgGuideDrawDefault", "rcbgGuideDraw(guide, 1). Only reached from a function-less chunk at 0x919CF1"),
    0x91DA30: ("rcbgGuideGetScreenPos", "world -> minimap screen, scale = size / 256"),
    0x91DA80: ("rcbgGuideDraw", "the guide map draw. Returns when g_rcbgGuideMasterEnable == 0 (0x91DBD1), no model (0x91DD57) or enable bit clear (0x91DD5D). Runs rcbgGuideFadeStep (0x91DD81), skips when the alpha is 0 (0x91DD94), else draws markers and calls FFX_GuideMap_MarkExplored (0x91EA3C), which writes the save block. RCBG.md 4.4"),
    0x91ECB0: ("rcbgGuideDrawFog", "'RenderMiniMapFog': a 0x70000 dyngeo buffer that shades the unexplored cells"),
    0x91F530: ("rcbgGuideArrowFade", "fade factor for a marker arrow by distance"),
    0x91F640: ("rcbgGuideSetMasterEnable", "g_rcbgGuideMasterEnable. Only caller is the orphaned debug page FFX_DebugPage_basicInfo_updateInput_ORPHAN"),

    # rcbgScene.c 0x91FFD0..0x921200, the 272 byte scene
    0x91FFD0: ("rcbgScene_ScaleMatrix", "rcbgScene.c matrix helper, scales by 100"),
    0x920020: ("rcbgScene_MulViewProj", "rcbgScene.c matrix helper"),
    0x920050: ("rcbgScene_TexSizeLog2", "GS texture size as log2"),
    0x9200A0: ("rcbgScene_VecScale", "rcbgScene.c vector helper"),
    0x920100: ("rcbgScene_ProjectDepth", "projects a point and returns its depth"),
    0x9201A0: ("rcbgScene_ProjectDepth2", "as rcbgScene_ProjectDepth for a second matrix"),
    0x920250: ("rcbgScene_EndPacket", "closes a GS packet through the rcPacket helpers"),
    0x9202E0: ("rcbgSceneBuildProjection", "tan(fov / 2) with flt_113701C, writes the projection, returns 512 / (2 * near * tan)"),
    0x9204B0: ("rcbgSceneBuildView", "eye and ref scaled by scene +244, FFX_Came_BuildLookAtMatrix with the roll +172"),
    0x920540: ("rcbgSceneAddObject", "appends an object to scene +12/+16, realloc (rcbgScene.c line 118)"),
    0x920580: ("rcbgSceneStepRoll", "the roll oscillator: +220 mode, +224 speed, +228 frames, +232 amplitude, +236 phase, +240 counter -> +172, sine table flt_C44BF0. Mode 2 decays. Ends with FFX_RcBg_ApplySceneCamera"),
    0x920650: ("rcbgSceneResetRoll", "stops the roll, +172 = 0"),
    0x920670: ("rcbgSceneRemoveObject", "removes one object (rcbgScene.c line 186)"),
    0x9206F0: ("rcbgSceneRemoveObjectsOfType", "removes and deletes every object of a type"),
    0x920740: ("rcbgSceneDrawObjects", "calls each object's Draw (+12), then queues nullsub_169 as an effect callback. From FFX_RcBg_StepSceneObjects"),
    0x920780: ("rcbgSceneBeginDrawThunk", "thunk to rcbgSceneBeginDraw"),
    0x920790: ("rcbgSceneBeginDraw", "+260 = 1, +256 = player view z * 0.1 (98 with no player), clears the set +16, sub_642EF0(+256)"),
    0x920830: ("rcbgSceneQueueDrawCallback", "FFX_Vfx_AddEffectCallback(nullsub_169, scene, 0, 1)"),
    0x920870: ("rcbgSceneFinal", "frees the layers, objects, +20 and the guide models at +28[]"),
    0x920950: ("rcbgSceneFindObject", "(type, n), the n-th object of a type"),
    0x9209A0: ("rcbgSceneCountObjects", "objects of a type"),
    0x9209E0: ("rcbgSceneInit", "272 byte scene, +244 = 10.0, +0 = 1"),
    0x920AA0: ("rcbgSceneProcess", "the per sub-step scene update: each object's Step (+8), FFX_RcBg_ApplySceneCamera when the layer count +4 != 0 (0x920AC9), rcbgSceneStepShake, rcbgSceneStepRoll, rcbgSceneStepLayers. RCBG.md 4.2"),
    0x920B00: ("rcbgSceneStepLayers", "per layer rcbgLayerStep, then pushes the frame (sub_643A20, sub_643A00) and hides a layer with +0 & 3 (sub_638F30, sub_644120). genk12, genk05 and level hash 3260 with language 9 are special cased"),
    0x920E40: ("rcbgSceneApplyMovieCamera", "from FFX_Fmv_ApplyMovieCamera. Projection and view without the camera override, returns 1"),
    0x9210E0: ("rcbgSceneStepShake", "the shake oscillator: +192 mode (odd writes +184, even +188, modes 3 and 4 decay), +196 speed, +200 frames, +204 amplitude, +208 random range, +212 phase, +216 counter. CRT rand at 0x921172, only when +192 != 0 and +208 > 0. Ends with FFX_RcBg_ApplySceneCamera"),
    0x921200: ("rcbgSceneResetShake", "stops the shake, +184/+188 = 0"),

    # rcbgPre.c 0x9215E0..0x9217AF, the 20 byte scene set
    0x921640: ("rcbgPreCreateDefaultSet", "a set with one scene when no YNPR was loaded (rcbgPre.c)"),
    0x9216B0: ("rcbgPreFreeSet", "frees every scene and the set, then FFX_Came_SetMatrixOverride(0, ..., 1024.0)"),
    0x921770: ("rcbgPreNewSet", "20 byte set +0 scene count, +4 scenes, +8 current, +12 a 64K buffer"),

    # rcbgMain.c 0x9217B0..0x9220B0, the 28 byte context g_ffxRcBgCtx (g_pMainWork in the source)
    0x9217B0: ("rcbgMainProcessCurrentScene", "rcbgSceneProcess on the current scene. From FFX_RcBg_Step"),
    0x9218C0: ("rcbgMainAddObject", "adds an object to the current scene, making a default set first if needed"),
    0x921910: ("rcbgMainDebugBattleRestart", "debug mode only (FFX_RcBg_Step): latches the encounter key into ctx +12 and clears ctx +8 so the FP object is made again"),
    0x9219E0: ("rcbgMainRemoveObjectsOfType", "on the current scene"),
    0x921A50: ("rcbgMainCreateFP", "the FP object for a scene id: 97 slik02, 173 blitzball, 565/566 ssbt00/01. Every other scene gets none"),
    0x921AA0: ("rcbgMainTeardown", "destroys the guide fog dyngeo, rcbgPreFreeSet, rcbgGuideDelete, frees chunks +16/+20/+24 and the context, g_ffxRcBgCtx = 0 (0x921B84)"),
    0x921BA0: ("rcbgMainGetFPSceneId", "encounter key 0x610000 -> 97, 0x2350000 -> 565, 0x2360000 -> 566, another key -> 0, no battle -> the save block scene id"),
    0x921BE0: ("rcbgMainFindObject", "rcbgSceneFindObject on the current scene"),
    0x921C20: ("rcbgMainCountObjects", "rcbgSceneCountObjects on the current scene"),
    0x921CE0: ("rcbgMainInit", "tears an old context down, allocates the 28 byte context, rcbgGuideNew at +4, identity on the four rcBg matrices. FFX_MainInit and every map unload"),
    0x921D60: ("rcbgMainLoadYnStream", "walks a YN chunk stream: YNPR set, YNSC scenes (272 byte stride), YNSM, YNST, YNTT/YNFT, YNGM. RCBG.md 5"),
    0x922010: ("rcbgMainStart", "default set if none, rcbgMainCreateFP(rcbgMainGetFPSceneId()), ctx +8 = 1. From FFX_RcBg_Step on the first sub-step"),

    # rcbgTrxTex.c 0x9220C0..0x922282, two TRX that upload together (type 0x20003, 0x20006 for particles)
    0x9220E0: ("rcbgTrxTexDelete", "Final then free"),
    0x922100: ("rcbgTrxTexDraw", "uploads both TRX"),
    0x922120: ("rcbgTrxTexFinal", "finalises both TRX"),
    0x922140: ("rcbgTrxTexGetCount", "frame count"),
    0x922160: ("rcbgTrxTexInit", "type 0x20003, 160 bytes, TRX at +32 and +96"),
    0x9221B0: ("rcbgTrxTexNew", "allocates and inits"),
    0x9221E0: ("rcbgTrxTexSetFrame", "sets the frame of both TRX"),
    0x922210: ("rcbgTrxTexSetTex0", "TEX0 of both TRX"),

    # rcbgFP.c 0x922290..0x923231, the field particle objects (type 0x20008, Step slot only)
    0x922290: ("rcbgFP_BuildPartMatrix", "rcbgFP.c matrix helper"),
    0x922300: ("rcbgFP_BindGroupToChrJoint", "binds an effect group to a CHR joint"),
    0x922350: ("rcbgFP_GetJointMatrixScaled", "joint matrix, scaled"),
    0x9223B0: ("rcbgFP_BindGroups", "walks a table of 28 word bind records"),
    0x9224B0: ("rcbgFP_VecScale", "rcbgFP.c vector helper"),
    0x922510: ("rcbgFP_VecCopy", "rcbgFP.c vector helper"),
    0x922560: ("rcbgFPSlikInit", "FP 97 (slik02, the S.S. Liki), 64 bytes, type 0x20008"),
    0x9225A0: ("rcbgFPSlikNew", "allocates and inits"),
    0x9225D0: ("rcbgFPSlikStepStart", "first Step: binds the 9 groups in word_C5E4B8, then swaps Step to rcbgFPSlikStep"),
    0x922600: ("rcbgFPSlikStep", "reads group 90 work through FFX_RcBg_GetFPWorkPtr80/64 and drives the other groups"),
    0x922930: ("rcbgFPSlikFadeInit", "show and hide state"),
    0x922970: ("rcbgFPSlikFadeStep", "four state show and hide on group 90 work y thresholds"),
    0x922B50: ("rcbgFPBltzStop", "+36 = 1. mapfx:104"),
    0x922B70: ("rcbgFPBltzInit", "FP 173 (bltz0000), 40 bytes"),
    0x922BB0: ("rcbgFPBltzNew", "allocates and inits"),
    0x922BE0: ("rcbgFPBltzStep", "switch on mode +24 with a frame counter +28, effect groups 78, 83..85, 88, 89, 97"),
    0x922C50: ("rcbgFPBltzMode0", "mode 0"),
    0x922DC0: ("rcbgFPBltzMode3", "mode 3"),
    0x922E60: ("rcbgFPBltzMode1", "mode 1"),
    0x922EB0: ("rcbgFPBltzMode2", "mode 2"),
    0x922EF0: ("rcbgFPBltzLinkChrs", "mapfx:103. Two CHRs and the mode"),
    0x922F50: ("rcbgFPSsbtInit", "FP 565/566 (ssbt00, ssbt01), 16 bytes"),
    0x922F80: ("rcbgFPSsbtNew", "allocates and inits"),
    0x922FB0: ("rcbgFPSsbtStepStart", "binds the 3 groups in word_C5E6B0, then swaps Step"),
    0x922FE0: ("rcbgFPSsbtStep", "group 90 op 6 work, then sub_90A520(10, mat) writes a 64 byte matrix into slot 10 of the table at 0x18DF59C"),
    0x923190: ("rcbgFPGetGroupOpWorkNth", "the n-th placed effect of a group, op work"),

    # rcbgModel.c 0x927B00..0x928573, the 0x140 byte model (type 0x20001)
    0x927B00: ("rcbgModel_VecScale", "rcbgModel.c vector helper"),
    0x927B60: ("rcbgModel_VecToS16", "float vec -> s16"),
    0x927BA0: ("rcbgModel_S16ToVec", "s16 -> float vec"),
    0x927BE0: ("rcbgModelBuildBlob", "builds the prim blob, sizes from byte_C5E790"),
    0x927D70: ("rcbgModelSetBlob", "attaches a prim blob at +20"),
    0x927FA0: ("rcbgModelBuildSimple", "a model from a vertex list"),
    0x928000: ("rcbgModelDelete", "Final then free"),
    0x928020: ("rcbgModelDraw", "bbox cull (sub_92AF60) when +16 bit 0, then FFX_GuideMap_Submit or sub_710ED0"),
    0x928240: ("rcbgModelFinal", "frees the blob"),
    0x928270: ("rcbgModelGetVertexEnd", "end of the vertex block"),
    0x9282A0: ("rcbgModelGetPrimData", "prim data"),
    0x9282C0: ("rcbgModelGetPrimHeader", "prim header"),
    0x928300: ("rcbgModelGetScale", "matrix +192 / 10"),
    0x928320: ("rcbgModelInit", "type 0x20001, 0x140 bytes"),
    0x9283B0: ("rcbgModelNew", "allocates and inits"),
    0x9283F0: ("rcbgModelReadS16Verts", "s16 vertices -> floats"),
    0x928460: ("rcbgModelSetAlpha", "alpha"),
    0x928540: ("rcbgModelSetScale", "scales the matrix"),

    # layer support, file name not anchored, 0x929910..0x92B19F
    0x929910: ("rcbgLayer_Log2", "log2 helper"),
    0x929930: ("rcbgClutGetLast", "pixels of the last frame of the clut TRX (clut +12). The clut work is 16 bytes at layer +176"),
    0x929950: ("rcbgClutCreateTrx", "TRX for the clut"),
    0x929990: ("rcbgClutGet", "pixels of frame n of the clut TRX"),
    0x9299B0: ("rcbgClutInit", "zeroes the clut work"),
    0x9299D0: ("rcbgClutCopy", "copies a clut"),
    0x929A10: ("rcbgClutRotate", "palette rotate"),
    0x929AB0: ("rcbgClutSetFrame", "word at clut +8"),
    0x929AC0: ("rcbgClutShiftZero", "palette shift"),
    0x929B60: ("rcbgClutStep", "steps the palette animation, type byte +186 and time byte +187"),
    0x92AC90: ("rcbgLayerFinal", "deletes the image TRX (+160) and the clut TRX (+188), rcbgAnimeFinal"),
    0x92ACF0: ("rcbgLayerGetClutPixels", "frame 0 pixels of the clut TRX at layer +188, or 0"),
    0x92AD20: ("rcbgLayerInit", "192 bytes, scale +68/+72 = 1.0, +44/+48 = 256, +56 = 68, +64 = 0x80808080"),
    0x92AD80: ("rcbgLayerStep", "anime -> TRX frame and clut frame, rcbgClutStep, rcbgLayerTween"),
    0x92ADE0: ("rcbgLayerTween", "the scale tween +68/+72 (pushes sub_639D60) and the +76/+80 tween"),
    0x92AF60: ("rcbgBBoxInFrustum", "bbox test for rcbgModelDraw"),
    0x92B0D0: ("rcbgSetProjGlobals", "writes the projection globals at 0x25D58E0, from FFX_RcBg_ApplySceneCamera"),

    # rcbgLoad.c 0x92B1A0..0x92B7C6
    0x92B270: ("rcbgLoadLayerCenter", "layer centre from its size"),
    0x92B2C0: ("rcbgLoad_Pad16", "rounds up to 16"),
    0x92B2F0: ("rcbgLoadReadModel", "YNGM: a model from +16 and the size"),
    0x92B390: ("rcbgLoadReadSceneSet", "YNPR: the scene set"),
    0x92B400: ("rcbgLoadSceneFinish", "+248 hi-reso when the height >= 448 ('@@@@ Pre-BG Hi-Reso Data'), +252"),
    0x92B460: ("rcbgLoadSceneLayoutVram", "lays the layer images out in VRAM"),
    0x92B5E0: ("rcbgLoadReadScene", "YNSC: eye +104, ref +120, up +136, depth +168, adj y +100, then FFX_RcBg_ApplySceneCamera"),
    0x92B6B0: ("rcbgLoadReadStaticModel", "YNSM"),
    0x92B6E0: ("rcbgLoadReadStaticTrx", "YNST"),
    0x92B780: ("rcbgLoadReadTrxTex", "YNTT and YNFT"),

    # rcbgTrx.c 0x92D230..0x92D681, the 64 byte TRX (type 0x20002)
    0x92D230: ("rcbgTrxUpload", "builds the GIF upload and hands it to rcbgTrxUploadHook, which is empty on PC"),
    0x92D320: ("rcbgTrxSetup", "image setup"),
    0x92D360: ("rcbgTrxSetTex0", "TEX0"),
    0x92D3E0: ("rcbgTrxAlloc", "allocates the image (rcbgTrx.c)"),
    0x92D450: ("rcbgTrxDelete", "Final then free"),
    0x92D470: ("rcbgTrxDraw", "uploads when the frame +60 != -1"),
    0x92D490: ("rcbgTrxFinal", "frees the image"),
    0x92D4C0: ("rcbgTrxGetCount", "frame count"),
    0x92D4D0: ("rcbgTrxGetImage", "image"),
    0x92D4F0: ("rcbgTrxGetPixels", "pixels"),
    0x92D510: ("rcbgTrxInit", "type 0x20002, 64 bytes"),
    0x92D580: ("rcbgTrxNew", "allocates and inits"),
    0x92D5A0: ("rcbgTrxBuildUploadPacket", "GIF packet for the upload"),
    0x92D670: ("rcbgTrxSetFrame", "+60"),
    0x92D680: ("rcbgTrxUploadHook", "empty. The PS2 VRAM upload has no PC body, the textures reach the GPU through Phyre"),

    # rcbgAnime.c 0x930830..0x930A08, the anime at layer +144
    0x930830: ("rcbgAnimeFinal", "frees the frame table"),
    0x930880: ("rcbgAnimeGetCurrent", "the current frame record"),
    0x9308A0: ("rcbgAnimeInit", "speed +20 = 256"),
    0x9308D0: ("rcbgAnimeSetFrameCount", "allocates the frame table +12 (rcbgAnime.c)"),
    0x930920: ("rcbgAnimeStep", "+24 += speed, frames advance per 256. Loop mode in flags & 0xF00, reverse in bit 2"),

    # rcbgDebug.c, rcbgSave.c, rcbgSaveText.c, rcbgLoadText.c, only the string anchors. All are
    # reached only through the orphaned rcui editor (RCBG.md 6)
    0x92C130: ("rcbgDebugDrawOverlay", "debug overlay callback. Only queued by the FFX_RcBg_Step tail, which needs g_rcbgDebugWork != 0"),
    0x938350: ("rcbgSaveFP", "writes host:/ffx/proj/map/f_particle/b%03d.fpd or m%03d.fpd. Dev tool"),
    0x938590: ("rcbgSaveGuideMap", "writes gmobjdata.bin on the host. Dev tool"),
    0x938A10: ("rcbgSaveGuideMap2", "second gmobjdata.bin writer. Dev tool"),
    0x939040: ("rcbgSaveBin", "writes host:%s/bin/map2d.bin. Dev tool"),
    0x939B10: ("rcbgSaveText", "writes host:%s/txt/%s.2d, the text form of the scene and layers (OffX +92, OffY +96, AdjY +100, layer Flag +0, X +12, Y +16, Z +20..+32, SpeedX +44, SpeedY +48, Alpha +56, anime +152, Pal +186/+187). Dev tool"),
    0x93A280: ("rcbgLoadTextScene", "reads the .2d text form from the host. Dev tool"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x919A70: ("FFX_RcBg_Init", "was j_rcbgMain__f921CE0. Thunk to rcbgMainInit, from FFX_MainInit (0x909030) and FFX_Map_UnloadMapOut"),
    0x91D3A0: ("rcbgGuideFadeReset", "was __cfltcvt_init_104. A FLIRT false match. It zeroes the four guide fade globals"),
    0x857C40: ("FFX_AtelSys_Core_243_SetGuideMapLayout_resi", "was FFX_AtelSys_Core_243_SetBgScene_resi. FFX_RcBg_SetGuideMapLayout, preset 0..4 of the guide map position and size. No bg scene is involved. RCBG.md 4.4"),
    0x857FC0: ("FFX_AtelSys_Core_325_SetGuideMapPos_resi", "was FFX_AtelSys_Core_325_SetRcBgParam_resi. FFX_RcBg_SetGuideMapPos, the guide map screen centre"),
    0x8581A0: ("FFX_AtelSys_Core_326_SetGuideMapSize_resi", "was FFX_AtelSys_Core_326_SetRcBgValue_resi. FFX_RcBg_SetGuideMapSize"),
    0x91B550: ("FFX_AtelSys_MapFx_038_StartSceneShake_resi", "was FFX_AtelSys_MapFx_038_StartSceneTween_resi. FFX_RcBg_StartSceneShake, a sine shake of the scene camera with an optional CRT rand amplitude, not a tween"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x193C0F0: ("g_rcbgDebugWork", "the rcui and rcbgDebug editor work. Written only at 0x9343C0 and 0x932F0B, both in the orphaned rcui editor. Past the initialised .data, so 0 in retail"),
    0xC5E478: ("g_rcbgGuideMasterEnable", "1 at boot. rcbgGuideDraw returns when 0. Only rcbgGuideSetMasterEnable writes it, from an orphaned debug page"),
    0xC5E350: ("g_rcbgGuideFlags", "bit 0 is the guide map enable, rcbgGuideSetEnabled"),
    0xC5E354: ("g_rcbgGuideLayout", "guide map layout preset 0..4"),
    0xC5E358: ("g_rcbgGuideSize", "guide map size, 128 or 256"),
    0xC5E35C: ("g_rcbgGuidePosX", "guide map screen centre x"),
    0xC5E360: ("g_rcbgGuidePosY", "guide map screen centre y"),
    0xC5E364: ("g_rcbgGuideFadeDelayFrames", "2. Frames rcbgGuideFadeStep waits before a fade starts"),
    0xC5E368: ("g_rcbgGuideFadeFrames", "6. Length of a guide map fade"),
    0x193BF6C: ("g_rcbgGuideFadeState", "0 hidden, 1 fading in, 2 shown, 3 fading out"),
    0x193BF70: ("g_rcbgGuideFadeDelay", "delay counter"),
    0x193BF74: ("g_rcbgGuideFadeLevel", "0..g_rcbgGuideFadeFrames"),
    0x193BF78: ("g_rcbgGuideAlpha", "level * 128 / frames. rcbgGuideDraw skips at 0"),
    0x193BF7C: ("g_rcbgGuideSavedScale", "FFX_RcBg_SaveGuideScale and RestoreGuideScale"),
    0x193BF28: ("g_rcbgFPMatrix", "64 byte matrix, FFX_RcBg_GetFPMatrix"),
    0xC5E4B8: ("g_rcbgFPSlikBinds", "the 9 group bind records rcbgFPSlikStepStart walks"),
    0xC5E6B0: ("g_rcbgFPSsbtBinds", "the 3 group bind records rcbgFPSsbtStepStart walks"),
    0xC5E790: ("g_rcbgModelPrimSizes", "prim size table for rcbgModelBuildBlob"),
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
