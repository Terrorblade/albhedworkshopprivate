"""
Apply the magic DLL host API names from reversing/MAGIC_HOST_API.md to the FFX.exe IDB.

g_ffxMagicHostApiTable at 0xC64CE8 is the 947 slot table FFX.exe hands to every magic_NNNN.dll through
InitMagicPRX (MAGIC_DLL.md). This names the placeholder targets of the slots a DLL reaches, by a call
instruction or a referenced forwarding thunk, plus the ones no DLL reaches where the body was plain,
and the data slots. Each comment says the slot number, how many DLLs reach it and the evidence tag.
Rows are grouped by subsystem as the doc groups them.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all. A name starting maybe_ is a guess
and says so. A positional name whose module part has a digit in it (pppDrawMdl2__f...) does not match
the placeholder pattern, so those rows are in RENAMES, as are maybe_ names this pass confirmed.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_host_api_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
COMMENTS = False
VERBOSE = False

# ---------------------------------------------------------------------------------------------
# Functions whose current name is a placeholder
# ---------------------------------------------------------------------------------------------

NAMES = {
    # ATEL helpers
    0x86AEE0: ('FFX_Atel_GetPartyCharChr_dup', "Host API slot 260 (32 DLLs call it). [V] Same body as FFX_Atel_GetPartyCharChr 0x86AE90, only the call displacements differ: returns party character a1's CHR when its actor is CHR backed (0x86AEE0). FFX_Oef2_Op100. 32 DLLs."),
    0x875FB0: ('FFX_Atel_ShowTextOverlay', 'Host API slot 306 (111 DLLs call it). [V] Text overlay slot a1 (clamped 0..7) loads entry a2 (<= 63) of table a3 into both layer records via sub_876180 and sets the state byte to 1 (0x875FB0). Core 491 and 492 call it. 111 DLLs.'),
    # CRT and memory
    0x9DBE90: ('FFX_MagicApi_MemAllocAligned', 'Host API slot 803 (182 DLLs call it). [V] Forwards (size, align) to sub_686FF0 -> sub_6FB770, which takes the allocator lock, allocates size + align, fills 0xCD and stores the pad byte, the allocation pair of j_j_FFX_MemFreeAlignedLocked (slot 804). magic_0021 slot 0 takes its 1 MB work area here.'),
    # Oef2 interpreter
    0x7EA120: ('FFX_Oef2_SplineEvalVec3', 'Host API slot 270 (7 DLLs call it). [V] Evaluates a cubic spline per component at time a2: copies each of x, y, z from the a5 key vec4s into a scratch buffer (Oef2 scratch stack 0x119FE10) and calls sub_7E9E20 with the knot times a4 and second derivatives. Callers FFX_Oef2_Move10, Op134. 7 DLLs call it.'),
    0x7F5F70: ('maybe_FFX_Oef2_DllOp_DrawTexMeshOrBranch', 'Host API slot 735 (reached only through DLL thunks, 581 DLLs). [I] Oef2 op handler reached from the DLL op table at header +0x7C. Mode 0x1000 draws a textured mesh for the object under dynamic geometry key 154 via sub_7F65D0 (skipped on catch up). Mode 0x2000 branches by the s16 at ip+2 unless sub_788DD0 (byte_112CA06, Btl 216) is set (0x7F5F70).'),
    0x7FA6D0: ('FFX_Oef2_DllOp_ScreenWarpMesh', 'Host API slot 911 (reached only through DLL thunks, 581 DLLs). [V] Oef2 op handler in the DLL op table at header +0x7C. Mode 0x1000 builds a textured screen mesh (key 706/2) from sub table 2 entry ip+2 via sub_7FA800. Mode 0x2000 builds a jittered FrameBuffer screen mesh (key 706/1) scaled by obj+0x40 via sub_7FA750. Returns ip+6 or ip+4.'),
    0xA65750: ('op_md_draw', 'Host API slot 308 (4 DLLs call it). [V] Dispatches a model draw on flag bits of *a2 (0x2000, 4, 0x80, 0x300) to op_md0_onef and five sibling draw bodies. In the op_md file between e_weapon_shadow and op_md_copy.'),
    0xA68C90: ('maybe_op_md_draw_hier', 'Host API slot 567 (7 DLLs call it). [I] For each node of a model list scales the node translation by 1000, multiplies by the parent matrix (sub_7E83B0) and the view (g_ffxEffectViewMatrix default), draws, then scales back. Callers FFX_Oef2_Op137, FFX_Opp_Kind14, Kind15.'),
    0xA68E10: ('maybe_op_md_draw_hier_alt', 'Host API slot 360 (no DLL reaches it). [I] No magic DLL calls this slot. The 0xA68C90 body with an extra matrix and a y nudge for magic 437 and 438.'),
    0xA69300: ('op_md_draw_simple', 'Host API slot 697 (1 DLLs call it). [V] Fills a default draw descriptor (flags 1052, size 136, colour 0x80808080, texture a3) and calls sub_A6B710, one of the op_md_draw bodies.'),
    0xA69DE0: ('maybe_op_md_set_packet_refs', 'Host API slot 620 (1 DLLs call it). [I] Unless magic 148, 569 or 208, walks the variable length records from a1 +32 (stride dword_C88AA8[type] * count + 16) and stores a2 and a3 at +8 and +12 of each.'),
    0xA6A3A0: ('maybe_op_md_push_draw_state', 'Host API slot 361 (1 DLLs call it). [I] Bumps byte_2332EA1, takes the next 6144 byte frame from dword_119FE0C and copies a 0x50 byte state from a2 into it, blending colours with sub_A68FF0.'),
    0xA6DBE0: ('maybe_u2_part_line_model', 'Host API slot 739 (reached only through DLL thunks, 581 DLLs). [I] Oef2 sub command (+0x7C table index 9 in magic_0021), phase from work+538 & 0xF000: 0 allocs and inits, 0x1000 steps, 0x2000 builds line prims (sub_A6E2E0, FFX_Vfx_CreateLineModelParts), 0x4000/0x5000 model trail buffers, 0x6000 a textured VFX model (sub_A6E650). Thunks in 581 DLLs.'),
    0xA6F4D0: ('maybe_u2_part_chr_shader', "Host API slot 736 (reached only through DLL thunks, 581 DLLs). [I] Oef2 sub command (+0x7C index 7). Phase 0x1000 drives the effect CHR's render instance (sub_642B10, sub_6439B0, sub_63B810), otherwise draws the bound omd per node through 0xA68E10 and sub_643770. Thunks in 581 DLLs."),
    0xA70910: ('maybe_u2_part_chr_break', "Host API slot 740 (reached only through DLL thunks, 581 DLLs). [I] Oef2 sub command (+0x7C index 11). Phase 0x4000 captures the effect CHR geometry (host 704 sub_803220) into fragments (sub_A71780), 0x5000 draws them through sub_A70D70 whose log reads 'opus_0582_break_draw', other phases sub_A72380, sub_A72B80, FFX_ScreenFade_RequestMode5. Thunks in 581 DLLs."),
    0xA73810: ('maybe_u2_part_model_draw', 'Host API slot 728 (reached only through DLL thunks, 581 DLLs). [I] Oef2 DLL sub-command handler that walks the model list at +168 and builds the model unless g_ffxIsCatchUpStep.'),
    0xA73D90: ('maybe_u2_part_wave_model', 'Host API slot 730 (reached only through DLL thunks, 581 DLLs). [I] Oef2 sub command and DLL particle operator (+0x7C index 16, +0x78 index 2). Advances per vertex values with the sine tables flt_C43BF0/flt_C44BF0 in scratch, builds and submits a VFX model, frees it with FFX_DynGeoModel_DestroyGroupByKey. Thunks in 581 DLLs.'),
    0xA74410: ('maybe_u2_part_model_init', 'Host API slot 729 (reached only through DLL thunks, 581 DLLs). [I] Oef2 DLL sub-command handler, mode 0: allocates the part work from sub table 4 (op_malloc), sets +187 = 0x82 and builds the model geometry (FFX_Vfx_BuildModelGeometry).'),
    0xA74C60: ('maybe_u2_part_big_work', 'Host API slot 732 (reached only through DLL thunks, 581 DLLs). [I] Oef2 DLL sub-command handler, mode 0 allocates 163360 bytes of part work and initialises it (sub_A753F0), mode 0x1000 steps it (sub_A75170).'),
    0xA74D30: ('maybe_u2_part_texture', 'Host API slot 733 (reached only through DLL thunks, 581 DLLs). [I] Oef2 DLL sub-command handler, mode 0x3000 decodes a TEX0 style word and uploads a texture (sub_7E4420, sub_6438C0).'),
    0xA756E0: ('maybe_u2_part_chr_center', "Host API slot 734 (reached only through DLL thunks, 581 DLLs). [I] Oef2 DLL sub-command handler measuring the effect chr bounds (sub_63A2B0) and setting its 'center' bone offset (sub_63D880)."),
    0xA76720: ('maybe_u2_part_reset192', 'Host API slot 737 (reached only through DLL thunks, 581 DLLs). [I] Oef2 DLL sub-command handler, mode 0 clears +192 of the linked object, mode 0x1000 runs sub_A76770.'),
    0xA76B60: ('maybe_u2_part_chr_light', 'Host API slot 738 (reached only through DLL thunks, 581 DLLs). [I] Oef2 sub command (+0x7C index 8). Phase 0x1000 takes the effect CHR world matrix and its inverse and writes two vectors into the render instance (sub_63B5D0 slots 0 and 1), phase 0x2000 walks sub table 4 (sub_803200). Light or shader params inferred. Thunks in 581 DLLs.'),
    # Yanagi effects
    0x9249A0: ('FFX_Yanagi_GetCasterFacing', "Host API slot 515 (3 DLLs call it). [V] Returns flt_25D5EA4, which FFX_Yanagi_BuildTargetTable sets to the caster's wrapped battle rotation (sub_92DFD0) at 0x924A20."),
    0x9249C0: ('FFX_Yanagi_GetTargetDepth', "Host API slot 516 (3 DLLs call it). [V] Field +16 of target record a1 in the 36 byte table at word_25D5C60, the z of the target offset rotated into the caster's frame (0x924A20 stores v[2] there)."),
    0x9249E0: ('maybe_FFX_Yanagi_GetTargetAngle', 'Host API slot 517 (3 DLLs call it). [I] Field +12 of target record a1, the result of sub_92EC20 on the normalised caster to target direction. sub_92EC20 is presumably an atan2, not read.'),
    0x924A00: ('FFX_Yanagi_GetTargetDistance', 'Host API slot 518 (3 DLLs call it). [V] Field +8 of target record a1, the horizontal caster to target distance (length with y zeroed) written at 0x924A20.'),
    0x924A20: ('FFX_Yanagi_BuildTargetTable', 'Host API slot 519 (3 DLLs call it). [V] For up to 16 targets of the Yanagi context (count dword_25D5BBC, ids dword_25D5BC0) gets a bone position, rotates the offset from the caster bone into the caster frame and records id, distance, angle and depth (36 bytes each). Also averages the positions into unk_25D5EA8.'),
    0x925130: ('FFX_Yanagi_StepScreenFade', 'Host API slot 526 (1 DLLs call it). [V] sub_924C70(&g_ffxYanagiCtx), the screen fade step FFX_Yanagi_StepProgram also runs. It interpolates the fade colours and queues FFX_Gfx_QueueFadeQuad1000.'),
    0x9251C0: ('FFX_Yanagi_FreeProgram', 'Host API slot 435 (40 DLLs call it). [V] Makes program+16 the current ppp heap, frees the sub program array at +24 (FFX_Ppp_HeapFreeBlock) and every task in the list at +28 (sub_930260). magic_0187 calls it from overlay slot 1, the free slot.'),
    0x925510: ('FFX_Yanagi_SetPppHeapAlt', 'Host API slot 439 (40 DLLs call it). [V] Same body as FFX_Yanagi_SetPppHeap 0x9254F0, *g_ffxPppCurrentHeap = task +16. A second host entry point for the same operation.'),
    0x925590: ('FFX_Yanagi_StartPartA', "Host API slot 458 (14 DLLs call it). [V] Gets sub table 4 entry a3 of the task's Oef2 unit and starts a part slot at a2 +816 with it (sub_92F470 marks the slot live). FFX_YanagiTask wait 0x930350 polls the same +816 slot."),
    0x925690: ('FFX_Yanagi_StartPartB', 'Host API slot 490 (3 DLLs call it). [V] Sub table 4 entry a3 started in the part slot at a2 +960 through sub_92F6B0, which also wires off_C498DC and the ppp matrices at 0x230FD60 and 0x230FDA0 into it.'),
    0x9256D0: ('FFX_Yanagi_TaskSetParticle', 'Host API slot 440 (38 DLLs call it). [V] (program, task, subProgram, part): sub_92EEE0 points task slot +640 at entry part of sub program subProgram (program+24), with matrix +496, colour +352 and +368. Slot +640 runs FFX_Ppp_StartPart in sub_92F010. Also called by sub_9253A0.'),
    0x925710: ('FFX_Yanagi_SetScreenFadeManual', 'Host API slot 527 (1 DLLs call it). [V] dword_25D5C14 (program+500) = a1, and when nonzero also +448 = 3. With +500 set FFX_Yanagi_StepProgram skips sub_924C70 (0x9254B0 test), so the DLL steps the fade itself via host 526. DLL 0529.'),
    0x92D7E0: ('FFX_Yanagi_RadToPs2Rot', 'Host API slot 496 (1 DLLs call it). [V] Scales a rotation vec4 in place by -651.8986 (-4096/2pi) and subtracts 1024 from y, radians to PS2 angle units with the -90 degree yaw offset. Used before sub_7E9A30 in 0x92D8C0 and 0x92D9A0.'),
    0x92D810: ('FFX_Yanagi_WrapAngle2Pi', 'Host API slot 497 (1 DLLs call it). [V] Wraps an angle into 0..2pi with fmod (0x92D810).'),
    0x92D860: ('FFX_Yanagi_HideChrSelfEffects', "Host API slot 454 (1 DLLs call it). [V] Pure forward to host 309 sub_7FBA40(chr), which sets bit 4 of the CHR's shadow registry flag byte. The same callee backs ATEL Core 463 HideSelfChrEffects."),
    0x92D870: ('FFX_Yanagi_ShowChrSelfEffects', "Host API slot 455 (1 DLLs call it). [V] Pure forward to host 310 sub_7FBAB0(chr), which clears bit 4 of the CHR's shadow registry flag byte. Same callee as ATEL Core 462 ShowSelfChrEffects."),
    0x92D880: ('FFX_Yanagi_StartMotionBlur', 'Host API slot 478 (3 DLLs call it). [V] (mode, strength, frames): program +536/+540/+544, counter +548 = 0, active +552 = 1. Each step sub_924BF0 feeds host 543 sub_82BEE0 (the motion blur setter ATEL Sg 010 uses), fading the strength out in mode 2, and sends 0 when the frames run out.'),
    0x92D8C0: ('FFX_Yanagi_BuildUnitJointMatrix', "Host API slot 498 (1 DLLs call it). [V] Builds a matrix from a PS2 rotation and position (sub_7E9A30), the unit's joint a2 matrix (sub_833B80) and a 0.001 scale (sub_92DE80) and multiplies them into a3."),
    0x92D9A0: ('FFX_Yanagi_BuildUnitJointMatrixAtPos', "Host API slot 479 (2 DLLs call it). [V] The sub_92D8C0 build plus the unit's battle position (FFX_Btl_GetUnitPos) and a translate back, so the result is relative to the unit."),
    0x92DD30: ('maybe_FFX_Yanagi_TintChrClut', "Host API slot 499 (1 DLLs call it). [I] Walks the unit chr's CLUTs (sub_83C9A0 count, sub_83C9B0, sub_83C9D0, sub_83C9F0) and rewrites each entry as (entry + add) * mul per channel, clamped."),
    0x92DE50: ('FFX_Yanagi_MtxScale001', 'Host API slot 500 (1 DLLs call it). [V] Identity matrix with 0.01 on the diagonal (0x92DE50).'),
    0x92DEB0: ('FFX_Yanagi_CopyChrClut', "Host API slot 501 (1 DLLs call it). [V] sub_83C710 on the unit chr, host slot 345, which allocates a CLUT copy ('%s:CLUT')."),
    0x92DF10: ('FFX_Yanagi_ReleaseChrClut', 'Host API slot 502 (1 DLLs call it). [V] FFX_Ch_ReleaseTexListACopy on the unit chr, the release for FFX_Yanagi_CopyChrClut.'),
    0x92DF90: ('FFX_Yanagi_EndCasterMotion', "Host API slot 432 (39 DLLs call it). [V] sub_787950(caster dword_25D5BB8, 0), host slot 213, which resets the actor's motion state for mode 0."),
    0x92DFB0: ('FFX_Yanagi_EffectSetReady', 'Host API slot 433 (40 DLLs call it). [V] FFX_Btl_EffectSetReady(g_ffxYanagiCtx +4, a1 != 0), the state 4 to 5 step from EFFECT_OVERLAY.md. 40 DLLs.'),
    0x92DFD0: ('FFX_Yanagi_GetUnitRot', 'Host API slot 477 (4 DLLs call it). [V] FFX_Btl_GetUnitRot(unit) wrapped to 0..2pi by FFX_Yanagi_WrapAngle2Pi.'),
    0x92E000: ('FFX_Yanagi_GetUnitBoneNode', 'Host API slot 480 (3 DLLs call it). [V] FFX_Ch_GetBoneNodeIndex(FFX_Yanagi_GetUnitChr(unit), bone, 0).'),
    0x92E020: ('FFX_Yanagi_GetJointDirFromRoot', 'Host API slot 476 (1 DLLs call it). [V] Joint a2 and root joint world matrices of the unit, the difference scaled by 10 into a3 (sub_92D720, sub_92D690).'),
    0x92E0A0: ('FFX_Yanagi_TransformByJoint', "Host API slot 474 (7 DLLs call it). [V] Scales point a3 by 1000, w = 1, and transforms it by the unit's joint a2 world matrix into a4."),
    0x92E110: ('FFX_Yanagi_GetUnitHeight', 'Host API slot 488 (2 DLLs call it). [V] (bone 0 y - bone 2 y) * 10 from FFX_Ch_GetBoneWorldPos on the unit chr.'),
    0x92E170: ('FFX_Yanagi_GetRootDir', "Host API slot 503 (1 DLLs call it). [V] The unit's root joint matrix (sub_92E270), one row copied out and normalised."),
    0x92E1C0: ('FFX_Yanagi_GetUnitChr', 'Host API slot 481 (5 DLLs call it). [V] In battle (FFX_Btl_GetPhase != 0) FFX_Btl_GetUnitChr(id), otherwise FFX_Atel_GetPartyCharChr(id), which here works as a chr lookup. Used by 19 exe functions in this file.'),
    0x92E270: ('FFX_Yanagi_GetBoneMatrixScaled', 'Host API slot 463 (8 DLLs call it). [V] sub_92E240 on the bone node of a2: the joint world matrix with each row scaled by 100 (sub_92E630).'),
    0x92E2F0: ('FFX_Yanagi_GetJointPos', 'Host API slot 456 (10 DLLs call it). [V] Joint a2 world matrix of the unit chr, translation row copied to a3.'),
    0x92E340: ('FFX_Yanagi_GetBonePos', 'Host API slot 457 (12 DLLs call it). [V] FFX_Yanagi_GetJointPos on the bone node of bone a2.'),
    0x92E360: ('FFX_Yanagi_GetBonePosGround', 'Host API slot 513 (3 DLLs call it). [V] FFX_Yanagi_GetBonePos with y zeroed.'),
    0x92E3A0: ('FFX_Yanagi_GetUnitRadius', 'Host API slot 489 (2 DLLs call it). [V] FFX_Ch_GetDataCollisionRadiusScale of the unit chr times 10.'),
    0x92E400: ('FFX_Yanagi_AngleDiff', 'Host API slot 514 (3 DLLs call it). [V] |a - b| folded into 0..pi (0x92E400).'),
    0x92E4F0: ('FFX_Yanagi_RandDirXZ', 'Host API slot 469 (1 DLLs call it). [V] Random unit vector in the xz plane from two CRT rand() draws, retried until non zero. A DLL reachable CRT rand caller (magic_0195).'),
    0x92E580: ('FFX_Yanagi_Lerp', 'Host API slot 482 (6 DLLs call it). [V] a + (b - a) * f(t), f linear, mode 1 a sine ease in out and mode 2 a sine ease out, through g_ffxSinTable4096.'),
    0x92E630: ('FFX_Yanagi_MtxScaleRows100', 'Host API slot 504 (1 DLLs call it). [V] Scales the first three rows of a matrix by 100.'),
    0x92E680: ('FFX_Yanagi_PlaySound', "Host API slot 444 (39 DLLs call it). [V] Sub table 6 entry a1 of the Yanagi context's Oef2 unit, then sub_7FFF80 and sub_7FFE10(entry, 64, 256), host slot 220, which plays a sound or a streamed battle sound."),
    0x92E6B0: ('FFX_Yanagi_PlaySoundIfIdle', 'Host API slot 445 (8 DLLs call it). [V] As FFX_Yanagi_PlaySound but only when FFX_SpuStream_PollRequest returns 0.'),
    0x92E730: ('FFX_Yanagi_ClearChrMatrixOverride', 'Host API slot 483 (3 DLLs call it). [V] FFX_Ch_SetI10C(unit chr, 0), clears the override that FFX_Yanagi_SetChrMatrixOverride sets.'),
    0x92E750: ('FFX_Yanagi_StartUnitTurn1', 'Host API slot 459 (3 DLLs call it). [V] sub_795EB0(unit, 1, 0, 0, a2, a3), host slot 537, the battle unit turn or move kind 1 setup the AtelSys Btl StartTurnKind syscalls use.'),
    0x92E770: ('FFX_Yanagi_ReverseScreenFade', "Host API slot 472 (9 DLLs call it). [V] sub_925530(&g_ffxYanagiCtx): swaps the fade's from and to colours and restarts it."),
    0x92E780: ('FFX_Yanagi_EndUnitMotion', 'Host API slot 524 (1 DLLs call it). [V] sub_787950(unit, 0), the FFX_Yanagi_EndCasterMotion call for any unit.'),
    0x92E7A0: ('FFX_Yanagi_ReverseTargets', 'Host API slot 521 (reached only through DLL thunks, 1 DLLs). [V] Reverses the target id list dword_25D5BC0 in place (count dword_25D5BBC).'),
    0x92E830: ('FFX_Yanagi_SetChrMatrixOverride', "Host API slot 484 (3 DLLs call it). [V] Copies matrix a2, scales its rows by the chr's FFX_Ch_GetScale times FFX_Ch_GetDataF30, and installs it with FFX_Ch_SetI10C (into a3, or the static dword_193C090 when a3 is null)."),
    0x92E960: ('FFX_Yanagi_StartUnitTurn2', 'Host API slot 460 (3 DLLs call it). [V] sub_795EB0(unit, 2, 0, a2, a3, a4), host slot 537 kind 2.'),
    0x92E990: ('FFX_Yanagi_SetUnitShowVote', 'Host API slot 507 (1 DLLs call it). [V] sub_796660(a1, a2), host slot 296, FFX_Btl_SetUnitShowVote(a1, 2, a2).'),
    0x92E9A0: ('FFX_Yanagi_ApplyPendingHits', 'Host API slot 446 (36 DLLs call it). [V] sub_78D980(g_ffxYanagiCtx +4, a1, a2), host slot 239, the same hit application AtelSys Btl_014_ApplyPendingHits runs. 36 DLLs.'),
    0x92E9C0: ('FFX_Yanagi_StartScreenFade', "Host API slot 473 (10 DLLs call it). [V] Converts four colour bytes to floats and starts the Yanagi context's screen fade to that colour over a5 frames (sub_9255D0)."),
    0x92EA40: ('FFX_Yanagi_StartScreenFadeFromTo', 'Host API slot 485 (3 DLLs call it). [V] As FFX_Yanagi_StartScreenFade with an explicit from colour (sub_925630).'),
    0x92EB10: ('FFX_Yanagi_SetUnitMotion', 'Host API slot 447 (28 DLLs call it). [V] sub_7882C0(unit, a2), which sets the battle actor motion (FFX_Mot_SetByModeIndex, FFX_Ch_SetBlendFrames). FFX_Oef2_Op063_ChrMotion uses the same call. 28 DLLs.'),
    0x92EB20: ('FFX_Yanagi_StartUnitMove', 'Host API slot 486 (5 DLLs call it). [V] Forwards all args to FFX_Btl_StartUnitMove.'),
    0x92EB70: ('FFX_Yanagi_SetUnitWeaponVisible', 'Host API slot 508 (1 DLLs call it). [V] (unit, show): CHR of side record 0 via host sub_795B40(unit, 0) (the weapon record per BATTLE_ACTOR.md), then FFX_Ch_SetHideBit2(chr, show == 0). DLL 0361.'),
    0x92EBA0: ('FFX_Yanagi_StartScreenShake', 'Host API slot 491 (1 DLLs call it). [V] (mode, phaseStep, frames, amplitude, randRange): program +504..+532. Each step sub_924DF0 feeds FFX_Came_SetScreenOffset a sine table value times (amplitude + rand() % randRange), x or y by mode bit 0, decaying in mode >= 3. Uses CRT rand().'),
    0x92EBF0: ('FFX_Yanagi_QueueStreamSound', 'Host API slot 448 (8 DLLs call it). [V] Sub table 6 entry a1, then sub_800390(entry, a2), which queues a type 2 sound entry in maybe_g_ffxSpuStreamRequest[a2 & 1].'),
    0x92EC60: ('FFX_Yanagi_ShowText', 'Host API slot 522 (4 DLLs call it). [V] Sub table 5 entry a1 passed to sub_875FB0(7, a2, entry), the text overlay the AtelSys Core_491 ShowTextOverlay syscalls use.'),
    0x92EE40: ('FFX_YanagiTask_SetStateAndRerun', 'Host API slot 461 (12 DLLs call it). [V] FFX_YanagiTask_SetState then task flags |= 2. The task runner sub_92ED90 loops while bit 2 is set, so the new state runs in the same frame. 12 DLLs.'),
    0x92F970: ('FFX_YanagiTask_SetFollowJoint', 'Host API slot 441 (20 DLLs call it). [V] Sets task flags +188 bits 1..2 to 2, stores unit a2 at +624 and joint a3 at +628.'),
    0x92F9A0: ('FFX_YanagiTask_SetFollowBone', 'Host API slot 442 (35 DLLs call it). [V] FFX_YanagiTask_SetFollowJoint with the bone node of bone a3. 35 DLLs.'),
    0x9301E0: ('FFX_YanagiTask_ClearDrawFlags', 'Host API slot 505 (1 DLLs call it). [V] Clears bits 4..11 of task +188.'),
    0x930200: ('FFX_YanagiTask_SetDrawFlags', 'Host API slot 443 (32 DLLs call it). [V] FFX_YanagiTask_ClearDrawFlags then ORs a2 into task +188. 32 DLLs.'),
    0x930350: ('FFX_YanagiTask_WaitPartA', 'Host API slot 487 (only an unreferenced DLL thunk). [V] Only an unreferenced DLL thunk reaches this slot. Pops the task state (maybe_FFX_YanagiTask_PopStateFlag2) once part slot +816 is no longer live.'),
    0x9303C0: ('FFX_YanagiTask_WaitScreenFade', 'Host API slot 471 (only an unreferenced DLL thunk). [V] Only an unreferenced DLL thunk reaches this slot. Wait state: pops once dword_25D5C10 (program+496, the fade active flag) is 0.'),
    0x930430: ('FFX_YanagiTask_WaitUnitMotion', "Host API slot 427 (reached only through DLL thunks, 10 DLLs). [V] Pops the task state once sub_787840 reports the unit's motion request done (-1). The unit is the per depth slot task[11 + 4 * depth]."),
    0x930460: ('FFX_YanagiTask_WaitUnitMoveDone', 'Host API slot 495 (only an unreferenced DLL thunk). [V] Only an unreferenced DLL thunk reaches this slot. Pops the task state once actor +1045 (sub_7AAAC0) is 0.'),
    0x930490: ('FFX_YanagiTask_WaitParticleEnd', 'Host API slot 428 (reached only through DLL thunks, 10 DLLs). [V] Wait state: pops once bit 0 of task slot +640 is clear (sub_930010), the particle set by host 440 finished. Thunks in 10 DLLs.'),
    0x9304B0: ('FFX_YanagiTask_WaitEffectHold', 'Host API slot 429 (reached only through DLL thunks, 10 DLLs). [V] Pops the task state once FFX_Magic_GetEffectHoldFlag returns 0.'),
    0x930500: ('FFX_YanagiTask_WaitSpuStream', 'Host API slot 462 (only an unreferenced DLL thunk). [V] Only an unreferenced DLL thunk reaches this slot. Pops the task state once FFX_SpuStream_PollRequest returns 0.'),
    0x930520: ('FFX_YanagiTask_WaitSoleTask', 'Host API slot 431 (reached only through DLL thunks, 11 DLLs). [V] Pops the task state when the task is both dword_25D5A44 and dword_25D5AFC of the Yanagi context, the head and tail of its task list.'),
    0x930550: ('maybe_FFX_Yanagi_CreateChrAfterimage', 'Host API slot 465 (1 DLLs call it). [I] Allocates a ppp block for a chr copy of the unit (sub_8037C0 size), fills it through sub_803220 or sub_803360 and records the chr world matrix. Only magic_0529.'),
    0x930600: ('FFX_Yanagi_FreeChrAfterimage', 'Host API slot 466 (1 DLLs call it). [V] sub_930710 then FFX_Ppp_HeapFreeBlock on the block, the free for slot 465.'),
    0x930630: ('maybe_FFX_Yanagi_DrawChrAfterimage', 'Host API slot 467 (1 DLLs call it). [I] View x matrix, chr render setup (sub_831300, sub_8311D0, sub_830160), FFX_Ch_UpdateRenderForMagic573 and FFX_ChrInstance_SetMaterialColour from RGBA bytes +40..+43 / 128.'),
    0x930800: ('FFX_Yanagi_AllocChrAfterimage', 'Host API slot 468 (1 DLLs call it). [V] 256 byte ppp block initialised by sub_930770. Only magic_0529.'),
    # app
    0x641100: ('FFX_GetMasterVersion', 'Host API slot 939 (1 DLLs call it). [V] Returns dword 0 of the language manager (FFX_GetLanguage reads +4). core:595 IsWesternMasterVersion tests it against 0, 3, 4 (0x858210). 1 DLL.'),
    0x641160: ('FFX_App_GetFogColor', 'Host API slot 786 (4 DLLs call it). [V] Copies the application floats +96, +100, +104 out. sub_908980 (slot 558) scales them by 255 into the fog colour it returns when the magic fog override (g_ffxMagicDrawFlags bit 2) is off.'),
    0x6411A0: ('FFX_App_GetFogCurve', 'Host API slot 789 (4 DLLs call it). [V] Returns app+0x78. magic_0098 saves it and restores it through slot 784 (fog +40, the curve, FFX_Gfx_InstallFogFromGlobalCamera reads camera +236 m_fogCurve). 4 DLLs.'),
    0x6411B0: ('FFX_App_GetFogFar', 'Host API slot 788 (4 DLLs call it). [V] Returns application float +116. Exe caller sub_908BB0 (slot 559).'),
    0x6411C0: ('FFX_App_GetFogLimit', 'Host API slot 790 (302 DLLs call it). [V] Returns application float +124. Exe caller sub_908BB0 (slot 559).'),
    0x6411D0: ('FFX_App_GetFogNear', 'Host API slot 787 (4 DLLs call it). [V] Returns application float +112. Exe caller sub_908BB0 (slot 559), the fog near, far and limit getter.'),
    0x641230: ('FFX_App_GetMagicFogColor', 'Host API slot 792 (1 DLLs call it). [V] Copies the application floats +80, +84, +88 out. sub_908980 (slot 558) uses these instead of +96 when g_ffxMagicDrawFlags bit 2 is set. Slot 791 writes them.'),
    0x641270: ('FFX_App_GetFloat216', 'Host API slot 799 (no DLL reaches it). [V] No magic DLL calls this slot. Returns application float +216.'),
    0x641280: ('FFX_App_GetFloat212', 'Host API slot 798 (no DLL reaches it). [V] No magic DLL calls this slot. Returns application float +212.'),
    0x641290: ('FFX_App_GetFloat220', 'Host API slot 800 (no DLL reaches it). [V] No magic DLL calls this slot. Returns application float +220.'),
    0x6412A0: ('FFX_App_GetFloat208', 'Host API slot 797 (no DLL reaches it). [V] No magic DLL calls this slot. Returns application float +208.'),
    0x643150: ('FFX_Fog_SetColor', 'Host API slot 781 (4 DLLs call it). [V] sub_6E36F0 on the fog object (sub_6E32C0): writes rgb to its params +16..+24 and refreshes the cached copy. Exe callers are the AtelSys MapFx 062..065 SetFogColor syscalls.'),
    0x643180: ('FFX_Fog_SetCurve', 'Host API slot 784 (4 DLLs call it). [V] sub_6E3760 writes fog src+40. FFX_Gfx_InstallFogFromGlobalCamera fills +40 from camera +236, which is PCamera m_fogCurve. MapFx 055 passes 1.0. 4 DLLs.'),
    0x6431A0: ('FFX_Fog_SetFar', 'Host API slot 783 (4 DLLs call it). [V] sub_6E3A30 on the fog object. Exe callers FFX_AtelSys_MapFx_055_SetFog, FFX_AtelSys_MapFx_058_SetFogFar, sub_90A350.'),
    0x6431C0: ('FFX_Fog_SetLimit', 'Host API slot 785 (345 DLLs call it). [V] sub_6E3AA0 on the fog object. Exe callers FFX_AtelSys_MapFx_055_SetFog, FFX_AtelSys_MapFx_060_SetFogLimit, sub_90A350.'),
    0x6431E0: ('FFX_Fog_SetNear', 'Host API slot 782 (4 DLLs call it). [V] sub_6E3B10 on the fog object. Exe callers FFX_AtelSys_MapFx_055_SetFog, FFX_AtelSys_MapFx_056_SetFogNear, sub_90A350.'),
    0x643270: ('FFX_App_SetMagicFogColor', 'Host API slot 791 (116 DLLs call it). [V] Writes the application floats +80, +84, +88, the fog colour sub_908980 reads under the magic override.'),
    0x6432C0: ('FFX_App_SetFloat216', 'Host API slot 795 (no DLL reaches it). [V] No magic DLL calls this slot. Application float +216 = a1.'),
    0x6432E0: ('FFX_App_SetFloat212', 'Host API slot 794 (no DLL reaches it). [V] No magic DLL calls this slot. Application float +212 = a1.'),
    0x643300: ('FFX_App_SetFloat220', 'Host API slot 796 (no DLL reaches it). [V] No magic DLL calls this slot. Application float +220 = a1.'),
    0x643320: ('FFX_App_SetFloat208', 'Host API slot 793 (no DLL reaches it). [V] No magic DLL calls this slot. Application float +208 = a1.'),
    # battle menu
    0x897770: ('FFX_BtlMenu_GetInfoPanelState', 'Host API slot 373 (1 DLLs call it). [V] Returns word_133F6A8, the state FFX_BtlOd_PressCircleGate steps and sub_895390 draws the unit info panel from (0x897770). DLL 0020.'),
    0x897DC0: ('maybe_FFX_BtlMenu_InitInfoPanel', 'Host API slot 374 (1 DLLs call it). [I] Reads layout rects and points dum28, dum29, dumm1..3, dum37/38, dum39, dum30 from menu layout 2, then zeroes the 0x5C byte panel state at word_133F6A8 (0x897DC0). DLL 0020 calls it before 375.'),
    0x898BD0: ('FFX_BtlMenu_OpenUnitInfoPanel', "Host API slot 375 (1 DLLs call it). [V] word_133F6A8 = 1, word_133F6AA = unit a1, then sub_89A160 fills the unit's panel rows. State 1 starts the panel sequence FFX_BtlOd_PressCircleGate and sub_895390 step (0x898BD0). DLL 0020."),
    # battle units
    0x782AE0: ('FFX_Btl_GetBattlePoint', 'Host API slot 292 (9 DLLs call it). [V] Writes the battle point vec4 to a1: the script point Btl 165 SetBtlPoint stored at qword_112A8D0 when byte 1 of dword_112C9FC is set (0x7A772D), else sub_9087F0, a scaled field vec4 at flt_18DF02C. Callers FFX_Oef2_Op140 and two others. 9 DLLs call it.'),
    0x784FF0: ('FFX_Btl_IsCharSwitchBlocked', 'Host API slot 698 (1 DLLs call it). [V] Returns ply_rom row byte 16 bit 0x4 for a character (FFX_KernelTable_GetRow on g_ffxKernelTablePlyRom). The battle menu pages skip pushing the Switch entry when it is set (BATTLE_MENU.md). 1 DLL (0203).'),
    0x787840: ('FFX_Btl_IsUnitMotionDone', 'Host API slot 411 (277 DLLs call it). [V] Returns 0 while the unit has a CHR, a non zero applied motion speed (+0x400) and FFX_Mot_IsRequestActive is set, and logs (unit, motion id +0x3F0) into the debug wait word word_112CA08. Returns -1 otherwise. Same shape as Btl 008 WaitMotion. 277 DLLs call it.'),
    0x787950: ('maybe_FFX_Btl_ReleaseUnitMotion', 'Host API slot 213 (288 DLLs call it). [I] Ends a scripted unit motion: if sub_788270 cleared the +0x3F3 scripted motion flag it clears +0x412 (unless mode 3), sets per mode state bytes and runs the unit battle event 0 or 8 through sub_7AB360. Oef2 op 65 ChrSpeed modes 1 and 2. 288 DLLs call it, which fits returning the caster to idle.'),
    0x787C10: ('FFX_Btl_GetUnitAiProp225', 'Host API slot 684 (9 DLLs call it). [V] Returns the signed byte at BTLUNIT +0xDDA, the field FFX_BtlAi_GetProperty case 225 reads. Meaning not known. 9 DLLs call it.'),
    0x787C30: ('maybe_FFX_Btl_EffectReturnToIdle', 'Host API slot 529 (158 DLLs call it). [I] Takes a packed effect handle and acts only for kind 2 (battle unit): clears +0x405, ends the scripted motion (sub_788270) and, when +0xE01 (set by Btl_StartCommandExec) is not 0xFF, runs that unit battle event with the direction fix byte forced on. 158 DLLs call it with their stored handle from a state switch.'),
    0x7882C0: ('FFX_Btl_PlayUnitMotion', 'Host API slot 228 (289 DLLs call it). [V] Plays motion a2 on a battle unit: refuses without a CHR or when petrified, sets the scripted motion flag +0x3F3 and motion id +0x3F0, FFX_Mot_SetByModeIndex, hokan and blend frames unless bit 0x8000, returns the sequence frame count. Oef2 op 63 ChrMotion. 289 DLLs call it.'),
    0x7883F0: ('maybe_FFX_Btl_HoldUnitMotion', 'Host API slot 688 (3 DLLs call it). [I] For an ally slot unit sets the scripted motion flag +0x3F3 to 1 and the motion id +0x3F0 to -1, so the unit counts as script controlled with no motion picked (0x7883F0). 3 DLLs call it.'),
    0x788430: ('FFX_Btl_SetUnitMotionSpeedOverride', 'Host API slot 530 (163 DLLs call it). [V] a2 >= 0 stores a2 at +0x402 and sets the override flag +0x404, a2 < 0 clears the flag, then sub_78AC20 recomputes and applies the unit motion speed (FFX_Ch_SetSpeedMulA). Called by FFX_Btl_PlayUnitMotion. 163 DLLs call it.'),
    0x788DD0: ('FFX_Btl_GetUnit13PartFlag', 'Host API slot 689 (2 DLLs call it). [V] Returns byte_112CA06, the flag Btl 216 SetUnit13PartFlag writes and Btl 217 applies to the unit 13 CHR model part. Same body as the dead Btl 219 getter. 2 DLLs call it.'),
    0x788DE0: ('FFX_Btl_UnitIdPassthrough', 'Host API slot 168 (338 DLLs call it). [V] Returns its byte argument unchanged (0x788DE0). Callers store the result as the effect owner unit (FFX_Yanagi_InitProgram 0x925296, sub_7FEBA0 0x7FEC99). 338 DLLs call it with the caster unit at effect start.'),
    0x788E10: ('FFX_Btl_GetUnitSummoner', 'Host API slot 294 (9 DLLs call it). [V] Returns BTLUNIT +0x6D1 m_summoner, the unit that summoned this one, 0xFF none. Callers FFX_Oef2_Op100 and sub_7DBEE0. 9 DLLs call it.'),
    0x788E90: ('maybe_FFX_Btl_ResetUnitsForEffectStart', "Host API slot 626 (2 DLLs call it). [I] For all 31 units: +0x412 = a1, clears the motion speed override +0x404, eases CHR m_anim378 to 0 over 30 frames (sub_796360) and, when a1 is set, clears flag 0x100 vote slot 2. FFX_Btl_StepMagicEffectStates calls it with the actor's +0x413 when an effect starts. 2 DLLs call it."),
    0x78CA20: ('FFX_Btl_PlayUnitReaction', "Host API slot 388 (20 DLLs call it). [V] Plays a hit reaction on the unit's link unit (+0x422): maps the reaction kind (BTLHIT.m_reaction) to a battle event number (12, 14, 15, 16, 9, 48, 27, 56 KO path) and runs it through sub_7AB360. Btl 215 PlayLinkReaction and FFX_Btl_ApplyPendingEffects call it. 20 DLLs call it."),
    0x78D980: ('FFX_Btl_ApplyPendingHits', "Host API slot 239 (275 DLLs call it). [V] Applies the staged hits of the unit's current command: FFX_Btl_ApplyPendingEffects on every unit in selector a2, sub_78EE80 on the rest, and kills self through FFX_Btl_OnUnitDefeated when nothing landed and the self KO flag is set. Btl 014 ApplyPendingHits and FFX_Oef2_OpApplyPendingHits. 275 DLLs call it."),
    0x78F950: ('FFX_Btl_GetUnitStatus2', 'Host API slot 369 (1 DLLs call it). [V] Returns BTLUNIT +0x616, the status 2 word (Scan, distills, Shield, Boost, Eject, Auto-Life, Curse, Defend, Guard, Sentinel, Doom). 1 DLL (0020).'),
    0x790260: ('FFX_Btl_IsUnitMotionDone_Api', 'Host API slot 240 (5 DLLs call it). [V] One call wrapper: return FFX_Btl_IsUnitMotionDone(unit) (0x790260). Only DLLs reach it, 5 of them.'),
    0x790290: ('FFX_Btl_PlayUnitMotion_Api', 'Host API slot 696 (2 DLLs call it). [V] One call wrapper: return FFX_Btl_PlayUnitMotion(unit, motion) (0x790290). Only DLLs reach it, 2 of them.'),
    0x794280: ('FFX_Btl_GetUnitPlaceLayout', 'Host API slot 599 (1 DLLs call it). [V] Ally slot units only: returns BTLUNIT +0x4FC m_placeLayout, the layout part of the battle position key, else 0. 1 DLL (0250), right before FFX_Btl_SetUnitPlaceLayout.'),
    0x7948A0: ('FFX_Btl_GetUnitHomeFacing', 'Host API slot 169 (10 DLLs call it). [V] Ally slot units only: returns m_homeFacing[m_facingIndex] (+0x514 indexed by +0x511), else 0.0. Callers FFX_Oef2_Op072_UnitMove and FFX_Oef2_Op114. 10 DLLs call it.'),
    0x794D70: ('FFX_Btl_GetUnitAiProp126', 'Host API slot 532 (3 DLLs call it). [V] Ally slot units only: returns BTLUNIT +0x41F, the byte FFX_BtlAi_GetProperty case 126 reads and only BtlAi_SetProperty writes. FFX_Oef2_Op161 branches on it. Meaning not known. 3 DLLs call it.'),
    0x794DB0: ('FFX_Btl_TestUnitAiProp125OrPetrified', 'Host API slot 533 (1 DLLs call it). [V] Ally slot units only: true when BTLUNIT +0x41E (AI property 125) is set or the unit is petrified (+0xDCE). FFX_Oef2_Op161 branches on it. 1 DLL (0449).'),
    0x794E80: ('FFX_Btl_GetUnitAiProp128', 'Host API slot 703 (1 DLLs call it). [V] Ally slot units only: returns BTLUNIT +0xE0C, the byte FFX_BtlAi_GetProperty case 128 reads and only BtlAi_SetProperty writes. Meaning not known. 1 DLL (0263).'),
    0x794F70: ('FFX_Btl_IsUnitAirborne', 'Host API slot 534 (130 DLLs call it). [V] Ally slot units only: returns BTLUNIT +0x505, the flag Btl 265 SetAirborneMask sets with ground mode 0 (AI property 26). The centre and top position getters switch on it. Callers FFX_Oef2_Op095, Op162. 130 DLLs call it.'),
    0x795610: ('FFX_Btl_GetUnitChrId', 'Host API slot 390 (65 DLLs call it). [V] Ally slot units only: returns BTLCHR +0x4 m_chrId, the chr id FFX_Btl_QueueChrLoad loads, else 0. 65 DLLs call it.'),
    0x795640: ('FFX_Btl_GetUnitMoveArcMode', 'Host API slot 535 (3 DLLs call it). [V] Returns the signed byte BTLUNIT +0x420, the 0..3 arc mode Btl 028 SetMoveArcMode writes and FFX_Btl_BuildArcPath switches on. 3 DLLs call it.'),
    0x795660: ('FFX_Btl_GetUnitMotionEventBits', 'Host API slot 347 (4 DLLs call it). [V] Ally slot units only: returns BTLUNIT +0x39C m_motionEventBits, bit k set by FFX_Btl_OnMotionEvent for motion event k. Caller FFX_Oef2_Op112_ChrI408. 4 DLLs call it.'),
    0x7956A0: ('FFX_Btl_GetUnitAppliedMotionSpeed', 'Host API slot 536 (1 DLLs call it). [V] Ally slot units only: returns the word at BTLUNIT +0x400, the motion speed sub_78AC20 last applied to the CHR with FFX_Ch_SetSpeedMulA (0x78AD1A). 1 DLL (0165).'),
    0x795780: ('FFX_Btl_IsUnitKO', 'Host API slot 640 (1 DLLs call it). [V] True for an ally slot unit whose BTLUNIT +0xDCC m_ko is set (0x795780). Target selector -25 uses it (MONSTER_AI.md). 1 DLL (0564).'),
    0x795840: ('FFX_Btl_GetUnitAiProp8', 'Host API slot 492 (2 DLLs call it). [V] Ally slot units only: returns BTLUNIT +0x63D, the byte AI property 8 sets. Caller FFX_BtlAi_GetProperty. Meaning not known. 2 DLLs call it.'),
    0x795A20: ('FFX_Btl_GetUnitRadius', 'Host API slot 251 (36 DLLs call it). [V] Returns BTLUNIT +0x4E4 (the last Btl 178 script float) times m_scale[2], 0.0 without a CHR. FFX_Btl_ResolveMoveTargetPos mode 10 adds it for both units plus the attack offset to get the stop distance. 36 DLLs call it.'),
    0x795B40: ('FFX_Btl_GetUnitSideChr', 'Host API slot 219 (149 DLLs call it). [V] Returns the CHR of side record a2 (weapon or armour) of unit a1. When that record has none and the unit has attached units, falls back to the CHR of the a2-th unit attached to it. Callers include FFX_Oef2_Op100, Op133, Op152to153. 149 DLLs call it.'),
    0x795C90: ('FFX_Btl_AttachUnitToUnitBone', "Host API slot 702 (2 DLLs call it). [V] Attaches unit a1's CHR to bone of unit a2's CHR with FFX_Ch_AttachToParentBone and moves the unit to that bone (FFX_Btl_SetUnitPos), or detaches it when a2 < 0. 2 DLLs call it (HELD_OBJECTS.md)."),
    0x795D40: ('FFX_Btl_SetUnitPlaceLayout', 'Host API slot 600 (1 DLLs call it). [V] Ally slot units only: writes a2 to m_placeLayout (+0x4FC) and to the saved copy +0x509, returns -1. Pairs with FFX_Btl_GetUnitPlaceLayout in DLL 0250.'),
    0x795DA0: ('FFX_Btl_SetFlag100VoteAll', 'Host API slot 625 (2 DLLs call it). [V] For all 31 units: FFX_Btl_SetFlag100Vote(unit, 2, a1), the vote slot 2 of the CHR flag 0x100 (no body collision). DLLs 0391 and 0448 call it next to the effect start reset.'),
    0x795DD0: ('FFX_Btl_SetUnitRot', 'Host API slot 666 (8 DLLs call it). [V] Ally slot units only: FFX_Ch_SetRot(unit CHR, radians), returns -1, or 0 without a CHR. Sets the facing only, not m_moveDir. 8 DLLs call it.'),
    0x795E20: ('FFX_Btl_SetUnitRotAndMoveDir', 'Host API slot 170 (8 DLLs call it). [V] Ally slot units only: FFX_Ch_SetRotAndMoveDir(unit CHR, radians), returns -1, or 0 without a CHR. Callers FFX_Btl_PlaceArrivedAeon, FFX_Btl_SetupWeaponChr, FFX_Oef2_SetUnitOrChrRot. 8 DLLs call it.'),
    0x795EB0: ('FFX_Btl_StartUnitTurn', 'Host API slot 537 (5 DLLs call it). [V] Starts a scripted direction change: kind, target mode, ramp and hold frames into +0x42B..+0x430, total m_b42E = hold + 2 * ramp, refused while the direction fix byte is set. Btl 003, 006, 087, 264, 274 StartTurnKind wrap it. 5 DLLs call it.'),
    0x795FF0: ('FFX_Btl_SetUnitHomeFacingOverride', 'Host API slot 576 (2 DLLs call it). [V] Ally slot units only: stores the facing a3 at +0x53C and the enable bit a2 & 1 at +0x510. sub_7958C0 (Btl 159, 195) then returns that facing instead of +0x49C. 2 DLLs call it.'),
    0x7962B0: ('FFX_Btl_SetUnitB183Request2', 'Host API slot 295 (88 DLLs call it). [V] One call wrapper: sub_7962D0(unit, 2, value), which runs FFX_Btl_SetChrB183Request slot 2 on the unit, its side records and its attached units. Caller FFX_Oef2_Op133. 88 DLLs call it.'),
    0x796480: ('FFX_Btl_SetUnitShadeAlpha', 'Host API slot 541 (98 DLLs call it). [V] Stores alpha v at BTLUNIT +0x40 and FFX_Ch_SetShadeAlpha(v, mode) on the unit CHR, both side record CHRs and recursively on attached units. 98 DLLs call it.'),
    0x796660: ('FFX_Btl_SetUnitShowVote2', 'Host API slot 296 (215 DLLs call it). [V] One call wrapper: FFX_Btl_SetUnitShowVote(unit, 2, value). Callers FFX_Oef2_Op151_ChrVisibility, FFX_BtlAi_SetProperty, FFX_Magic_FlushDeferredUnload. 215 DLLs call it.'),
    0x7966A0: ('FFX_Btl_SetUnitShowVote1', 'Host API slot 238 (30 DLLs call it). [V] One call wrapper: FFX_Btl_SetUnitShowVote(unit, 1, value). Callers FFX_Oef2_Op151_ChrVisibility, Btl 047 ResetUnit, FFX_Btl_PlaceArrivedAeon and others. 30 DLLs call it.'),
    0x7968E0: ('FFX_Btl_SetUnitPushWeightMode', 'Host API slot 542 (2 DLLs call it). [V] Ally slot units with a CHR: FFX_Ch_SetF500 (CHR m_pushWeight) to m_weight (+0x4DC) when a2 > 0, 0.0 when a2 == 0, -1.0 (immovable) when a2 < 0. Callers FFX_Btl_FinishActions, FFX_Btl_StartCommandExec. 2 DLLs call it.'),
    0x79A680: ('FFX_Btl_SumPendingHitAmounts', "Host API slot 291 (229 DLLs call it). [V] For attacker a1's current command, sums the not yet applied hits (m_next..m_count) that target a2 holds in its two BTLHITLOG blocks: HP, MP and CTB amounts out through a3..a5, returns the hit count. Callers FFX_Oef2_Op030, Op175, FFX_Btl_BuildHitTargetList. 229 DLLs call it."),
    0x79A760: ('FFX_Btl_GetNextPendingHitAmounts', "Host API slot 618 (2 DLLs call it). [V] For attacker a1's current command, returns the pending hit count on target a2 and the HP, MP, CTB amounts of only the next hit (index m_next) of each matching BTLHITLOG block through a3..a5. 2 DLLs call it."),
    0x79A860: ('FFX_Btl_GetHitLogReaction', "Host API slot 622 (10 DLLs call it). [V] Finds target a2's BTLHITLOG block staged by attacker a1's current command and returns its byte +0x15, 0 when none. Dead Btl 228 GetHitReaction wraps it. 10 DLLs call it."),
    0x79AA50: ('FFX_Btl_GetPendingStatusChanges', "Host API slot 367 (215 DLLs call it). [V] Compares the pending hits of attacker a1 on target a2 with the target's live status words: status 1 gained/lost bits, status counter gained/lost bits, status 2 gained/lost bits out through a3..a8. Returns 1 when the pending HP damage reaches 0 HP or KO is set. 215 DLLs call it."),
    0x79AD90: ('FFX_Btl_GetUnitAiProp267', 'Host API slot 580 (2 DLLs call it). [V] Tests bit 0x200 of BTLUNIT +0x5B8 m_monsterFlags, which FFX_BtlAi_GetProperty case 267 also reads. Meaning of the bit not known. 2 DLLs call it.'),
    0x79E7C0: ('FFX_Btl_GetUnitStatusFxMask', 'Host API slot 493 (1 DLLs call it). [V] Packs the statuses that carry a visual into a mask: 1 Zombie, 2 Petrify (+0xDCE), 4 Berserk, 8 Haste, 0x10 Slow, 0x20 Curse (status 1 at +0x606, counters +0x613/+0x614, status 2 +0x616, read at 0x79E7D3..0x79E818). Caller sub_79EFF0. 1 DLL (0361).'),
    0x79EAE0: ('FFX_Btl_SetAllUnitsChrEffectsShown', 'Host API slot 566 (3 DLLs call it). [V] For all 31 units with a CHR and +0x4C set: sub_7FBAB0 (show, the Core 462 path) when a1 is set, else sub_7FBA40 (hide, Core 463), through sub_79EA40. Returns how many units it touched. 3 DLLs call it.'),
    0x7A9A20: ('FFX_Btl_GetUnitMotionId', 'Host API slot 415 (5 DLLs call it). [V] Returns BTLUNIT +0x3F0, the motion id FFX_Btl_PlayUnitMotion stores. Callers sub_792F20 (battle debug line) and sub_7DBEE0. 5 DLLs call it.'),
    0x7AB010: ('FFX_Btl_StartPathMove', 'Host API slot 598 (1 DLLs call it). [V] Starts move kind 7 along path slot a1 (0..2, needs a point): move kind +0x415 = 7, frames, spline index, speed a4 (0 when the direction fix byte is set), saves the rotation. Btl 073 StartPathMove and 118 StartArcTimeline. 1 DLL (0243).'),
    0x7AB0B0: ('FFX_Btl_AddPathPoint', 'Host API slot 597 (1 DLLs call it). [V] Appends a point to path slot a2 (0..2, 32 deep): copies the explicit point when given, then resolves it with FFX_Btl_ResolveMoveTargetPos for unit a3 and kind a4. Btl 071, 072 and FFX_Btl_BuildArcPath. 1 DLL (0243).'),
    0x7AB140: ('FFX_Btl_ClearPath', 'Host API slot 596 (1 DLLs call it). [V] Resets the point count of path slot a1 (0..2) at dword_1136418. Btl 070 ClearPath and FFX_Btl_BuildArcPath. 1 DLL (0243).'),
    0x7B1B50: ('MsSetReel', "Host API slot 808 (10 DLLs call it). [V] Wakka's slots overdrive: for 4 reels resolves the reel result into an ability (word_C4395E, FFX_Btl_ResolveAbilityId) and stages its hits on targets, random ones through FFX_Rand_Stream(19). Its own strings 'REEL%d' and 'Virtuos FIXME ... when calling MsSetReel' (printed with this function's time args when the unit is 4) give the name. 10 DLLs call it."),
    0x7B6C50: ('FFX_Btl_BuildUnitListByKind', 'Host API slot 288 (109 DLLs call it). [V] Fills byte_112C876 with unit indices by list kind a1 (5 summoned, 8 present 0..17, 9 and 16 the live party order plus present units, 10 aeon order, 11 live enemies, 19 enemy slots with a type), pads with 0xFF, returns the list and the count through a2. 109 DLLs call it.'),
    0x7B6FB0: ('FFX_Btl_BuildTargetOrderWithAttached', 'Host API slot 368 (2 DLLs call it). [V] FFX_Btl_BuildTargetOrder for unit a1 copied into +0xD60, then every unit linked to those targets through attach parents (sub_7B7440) appended. Returns the list, count through a2. 2 DLLs call it.'),
    0x7B7170: ('FFX_Btl_BuildHitTargetList', "Host API slot 412 (7 DLLs call it). [V] Builds +0xD80 of unit a1: each target from FFX_Btl_BuildTargetOrder repeated once per pending hit (FFX_Btl_SumPendingHitAmounts), max 64 ('list_over'). Shuffles with FFX_Rand_Stream(16) only when a3 != 0. 7 DLLs call it."),
    0x7B7280: ('FFX_Btl_GetHitTargetList', 'Host API slot 617 (2 DLLs call it). [V] Returns the +0xD80 hit target list of an ally slot unit (else null) and its count byte_112CA0E through a2. 2 DLLs call it.'),
    0x7B72C0: ('FFX_Btl_SortTargetOrderAcrossLine', "Host API slot 407 (3 DLLs call it). [V] Target order of unit a1 sorted by each target's signed distance (sub_7B6F10, stored at +0xF68) from the line between the centres of selectors -14 and -15, a3 picks the axis. Returns the list, count through a2. FFX_Yanagi_InitProgram calls it. 3 DLLs call it."),
    0x7B7D00: ('maybe_FFX_Btl_GetTopAggroChar', 'Host API slot 627 (2 DLLs call it). [I] Returns the party character 0..6 that is in the party, has ply_rom flag 0x2 (+0x590), is not KO or petrified and is allowed on an airborne field, with the highest dword_112CABC counter, else -1. sub_7845E0 bumps that counter from damage staging and enemy target picking. 2 DLLs call it.'),
    0x7B7D70: ('maybe_FFX_Btl_ReturnUnitToField', 'Host API slot 635 (1 DLLs call it). [I] Undoes maybe_FFX_Btl_SendUnitOffField: when +0xDFF is set, m_placeGroup -= 3, clears +0xDFF, resets motion and hold, re-places the unit (sub_794EC0, sub_796220), refreshes status fx and runs battle event 50. 1 DLL (0397).'),
    0x7B7E20: ('maybe_FFX_Btl_SendUnitOffField', 'Host API slot 636 (1 DLLs call it). [I] When +0xDFF is clear: m_placeGroup += 3 (the same shift FFX_Btl_PartyFlee uses), +0xDFF = 1, copies the group to +0x50A, clears +0x436, sets byte 1 of dword_112CA0F and runs battle event 18. 1 DLL (0397).'),
    0x7B7E80: ('FFX_Btl_IsUnitBusy', 'Host API slot 637 (1 DLLs call it). [V] True while BTLUNIT +0x432 is set, the busy flag Btl 049 SetBusy raises and Btl 062 WaitBattleIdle waits on. 1 DLL (0397).'),
    0x7B7F20: ('FFX_Btl_GetPendingGilCost', 'Host API slot 592 (2 DLLs call it). [V] Returns g_ffxBtlPendingGilCost (0x7B7F20). 2 DLLs call it (0018, 0019).'),
    0x7B7F30: ('FFX_Btl_SetSharedIntSlot', 'Host API slot 523 (11 DLLs call it). [V] dword_1136B18[a1] = a2 for a1 0..15. Only the dead Btl 198 getter reads the table back. 11 DLLs call it.'),
    0x7B7F50: ('FFX_Btl_SetSharedFloatSlot', 'Host API slot 389 (5 DLLs call it). [V] flt_1136AD8[a1] = a2 for a1 0..15. Only the dead Btl 199 getter reads the table back. 5 DLLs call it.'),
    # blur effect
    0x639110: ('FFX_MagicApi_BlurExRequest', "Host API slot 821 (54 DLLs call it). [V] Gets the BlurEffectEx singleton (sub_6C4A70, prints 'BlurEffectEx: singleton is not created yet.' when missing) and stores a1 at +44 with the request byte +40 set to 1 (sub_6C4A90). Exe caller FFX_Opp_Kind07."),
    0x643830: ('FFX_MagicApi_BlurExSetPositiveParams', 'Host API slot 822 (38 DLLs call it). [V] BlurEffectEx floats +92..+104 = a1..a4, each written only when > 0 (sub_6C4620). Exe caller FFX_Opp_Kind07. What the four floats control is not traced.'),
    0x643860: ('FFX_MagicApi_BlurExSetVec4', 'Host API slot 824 (38 DLLs call it). [V] BlurEffectEx floats +116..+128 = a1..a4 unconditionally (sub_6C46E0). Exe caller FFX_Opp_Kind07.'),
    0x643890: ('FFX_MagicApi_BlurExSetPair', 'Host API slot 823 (38 DLLs call it). [V] BlurEffectEx floats +108 and +112 = a1, a2 (sub_6C4710). Exe caller FFX_Opp_Kind07.'),
    # camera
    0x7877D0: ('FFX_Btl_GetCameraHandle', 'Host API slot 287 (43 DLLs call it). [V] Returns the battle camera handle for class a2 (0..7), opening it with FFX_Came_OpenHandle on first use and caching it in dword_112C22C[a2]. FFX_Btl_CloseAllCameraHandles closes the same array. Called by FFX_Oef2_Op057_Camera. 43 DLLs call it.'),
    0x79DDB0: ('FFX_BtlSpecial_SetSuspended', "Host API slot 281 (43 DLLs call it). [V] Writes the high byte of word_112A8E6, saving the old value once (byte_112A8E8, byte_112A8E9) for sub_79DD90 to restore. While non zero sub_79DD70 returns 0 so FFX_Came_StepAll skips the BtlSpecial camera (sub_79DFE0). Debug label 'ps'. Callers FFX_Oef2_Op057_Camera. 43 DLLs call it."),
    0x7BC750: ('FFX_Came_SetScreenDrwRect', 'Host API slot 690 (2 DLLs call it). [V] Screen a1 (0..2): sets the DRW target (+0x90) and the live rect (+0x50) to (x, y, w, h). Came 080 SetScreenDrwRect. 2 DLLs call it.'),
    0x7BD860: ('FFX_Came_StartTrapezoidMove', 'Host API slot 916 (1 DLLs call it). [V] For the active camera handle: arms a trapezoid tween (type 2, accel, hold, decel, start divisor) on each axis the record +7 mask does not lock, on the REF or CAM sub-block by a2 & 1. Came 020 and 021 CamMoveTrapezoid. 1 DLL (0475).'),
    0x7BED60: ('FFX_Came_StopMove', 'Host API slot 283 (43 DLLs call it). [V] For the active camera handle: zeroes the tween counters and sets the pending position to the committed one (FFX_Came_SetPos), and for the eye re-applies the roll and ScrDpt. Came 022 CamStopMove and 052 RefStopMove. 43 DLLs call it.'),
    0x7BFA80: ('FFX_Came_SetRoll', 'Host API slot 285 (43 DLLs call it). [V] Sets the roll target of the camera handle record and slot +0xC94, snapped into +0xC90 when no roll tween runs. Pairs with FFX_Came_GetRoll. Came 058 SetRoll. 43 DLLs call it.'),
    0x7BFBD0: ('FFX_Came_SetScrDpt', 'Host API slot 286 (44 DLLs call it). [V] Sets the ScrDpt (projection distance) target, slot +0xCA0, snapped into +0xC9C when no tween runs. Pairs with FFX_Came_GetScrDpt, MsCameraSetScrDpt wraps it. 44 DLLs call it.'),
    # camera and screen
    0x82BD10: ('FFX_Sg_SetAccBufAlpha', 'Host API slot 693 (2 DLLs call it). [V] Accumulation buffer alpha: target level a1 tweened over a2 frames (step word_1300712), instant when a2 is 0, event 258 special case (0x82BD10). Sg 026 SetAccBufAlpha calls it. 2 DLLs.'),
    0x82BDC0: ('FFX_Sg_SetOffscreenClear', 'Host API slot 694 (3 DLLs call it). [V] Stores mode and rgb bytes at byte_1300771..774, read by FFX_Gs_BeginFrameDrawEnv (0x82BDC0). Sg 057 SetOffscreenClear calls it. 3 DLLs.'),
    0x82BEE0: ('FFX_Sg_SetMotionBlur', 'Host API slot 543 (224 DLLs call it). [V] Motion blur: strength a1 enables it and sets level a1/128 (sub_643730, sub_643750), 0 disables (0x82BEE0). Sg 010 SetMotionBlur, came 125 via sub_7BCDD0, FFX_Btl_BeginBattle. 224 DLLs.'),
    0x82CBA0: ('FFX_ScreenFade_RequestMode4', 'Host API slot 583 (1 DLLs call it). [V] FFX_ScreenFade_Request(frames, 1, 1), which stores fade mode 4. Fills the gap between RequestMode2 and RequestMode3 (0x82CBA0). Caller FFX_Field_StepRandomEncounter. DLL 0671.'),
    0x82CC20: ('FFX_Sg_SetScreenTint', 'Host API slot 391 (5 DLLs call it). [V] Additive screen tint, fade channel type 7: rgb and level a4, level 0 clears (0x82CC20). Sg 008 SetScreenTint calls it. 5 DLLs.'),
    0x82CCA0: ('FFX_Sg_StartScreenColorFade', 'Host API slot 420 (1 DLLs call it). [V] Screen fade channel type 6 toward rgb over a1 frames (0x82CCA0). Sg 003 ScreenColorFade and SG_DebugWin_FlashProc call it. DLL 0661.'),
    # character access
    0x82EB60: ('FFX_Ch_SetFlags1Bit2000', 'Host API slot 6 (88 DLLs call it). [V] Sets CHR m_flags1 (+0x194) bit 0x2000 to bit 0 of a2, returns the changed bit (0x82EB60). Callers FFX_Oef2_Op151_ChrVisibility, sub_930550. 88 DLLs.'),
    0x830160: ('FFX_Ch_SetMeshGroupLayer', 'Host API slot 276 (125 DLLs call it). [V] CHR +0x187 = a2 and byte +21 of m_instance via sub_63B7D0, the mesh group layer FFX_GfxCtx_LinkMeshGroupLayer compares (0x830176). Callers FFX_Oef2_Op211, sub_930630. 125 DLLs.'),
    0x8303F0: ('FFX_Ch_GetJointCount', 'Host API slot 263 (5 DLLs call it). [V] Returns u16 +10 of m_skeleton (CHR +0x1C0), the joint count (0x8303F0). Callers FFX_Oef2_Op152to153, rcuiPre. 5 DLLs.'),
    0x830AE0: ('FFX_Ch_SetLightOverrideMask', 'Host API slot 398 (3 DLLs call it). [V] CHR +0x188 m_lightOverrideMask = a2 (0x830AE0). Ch 106 SetLightOverrideMask and FFX_MapFx_CommitPointLights call it. 3 DLLs.'),
    0x8311D0: ('FFX_Ch_ResetTexListA', "Host API slot 354 (26 DLLs call it). [V] sub_831F00(m_texListA, m_texListACount): restores every list A entry's start frame and refreshes it (0x8311D0). Twin of host 355. 26 DLLs."),
    0x831300: ('FFX_Ch_ResetTexListB', 'Host API slot 355 (26 DLLs call it). [V] sub_831F00(m_texListB, m_texListBCount) then sub_831110, the texture animation refresh (0x831300). Ch 113 ResetTexListB calls it. 26 DLLs.'),
    0x831390: ('FFX_Ch_SetTexListAFrame', "Host API slot 356 (49 DLLs call it). [V] sub_832050(m_texListA, count, frame a3, index a2): sets one list A entry's frame, index -1 means all (0x831390). Ch 091 and FFX_Ch_Allocate call it. 49 DLLs."),
    0x8313C0: ('FFX_Ch_SetTexListBFrame', 'Host API slot 357 (49 DLLs call it). [V] sub_832050(m_texListB, count, frame a3, index a2), the list B twin, index -1 means all (0x8313C0). Ch 090 SetTexListBFrame calls it. 49 DLLs.'),
    0x833940: ('FFX_Ch_SetEffCallback', 'Host API slot 579 (2 DLLs call it). [V] CHR +0x7F8 m_effCallback = a2, +0x7FC m_effContext = a3 (0x833940). DLLs 0434, 0610.'),
    0x833FC0: ('FFX_Ch_GetScaledWorldMatrix', 'Host API slot 13 (6 DLLs call it). [V] Copies CHR +0x210, the scaled world matrix, to a2 when non null and returns its address (0x833FC0). Twin of FFX_Ch_GetWorldMatrix (+0x1D0). 6 DLLs.'),
    0x8355E0: ('FFX_Ch_GetBone0WorldPos', 'Host API slot 410 (1 DLLs call it). [V] FFX_Ch_GetBoneWorldPos(chr, 0, out) (0x8355E0). DLL 0372.'),
    0x835600: ('FFX_Ch_GetBodyCentre', 'Host API slot 29 (116 DLLs call it). [V] Copies CHR +0x694 m_bodyCentre (4 floats) to a2 (0x835600). 116 DLLs.'),
    0x8356B0: ('FFX_Ch_GetBonePointJointWorldMatrix', 'Host API slot 621 (1 DLLs call it). [V] FFX_Ch_LookupBonePoint(chr, point a2) gives a joint, then FFX_Ch_GetJointWorldMatrix(chr, joint, out a4) (0x8356B0). DLL 0367.'),
    0x8360A0: ('FFX_Map_GetWaterLevel', 'Host API slot 705 (1 DLLs call it). [V] Returns g_ffxWaterLevel (0x8360A0), the read side of FFX_Map_SetWaterLevel. Callers FFX_Oef2_Op140, Op161. DLL 0374.'),
    0x839860: ('FFX_Ch_GetMotionFrame', 'Host API slot 22 (7 DLLs call it). [V] Returns CHR +0x740 m_frame, 24.8 fixed point (0x839860). Ch 081 GetMotionFrame and Ch 117 call it. 7 DLLs.'),
    0x83D070: ('FFX_Ch_SetTexAnimSlotByteE', "Host API slot 587 (1 DLLs call it). [V] When m_texAnimProgram is set, writes byte +0xE of the CHR's texture animation slot (byte_231000E[48 * m_texAnimHandle]) via sub_77FE30, read by FFX_ChTexAnim_Step (0x77F3E3). Ch 141 calls it. DLL 0672."),
    # chr render instance
    0x639170: ('FFX_ChrInstance_PushMeshMatrixOverride', 'Host API slot 913 (4 DLLs call it). [V] For each mesh of a render instance, FFX_Gfx_PushModelMatrixOverride(g_ffxGfxCtx, mesh, a2) on the mesh whose id (+24) is a3, or on all meshes when a3 is -1. Exe caller FFX_Opp_Kind18.'),
    0x63A2B0: ('FFX_ChrInstance_GetBounds', 'Host API slot 920 (no DLL reaches it). [V] No magic DLL calls this slot. FFX_Ch_GetRenderInstance(a1), then min and max over every mesh bounding box, starting from plus and minus 16777216.'),
    0x63A410: ('maybe_FFX_ChrInstance_GetBoundsTransformed', 'Host API slot 924 (2 DLLs call it). [I] Same min and max walk as slot 920 but each mesh box goes through sub_4F96A0 first, which looks like a transform by the mesh matrix. Called by slot 934.'),
    0x63A610: ('maybe_FFX_ChrInstance_GetBoundsWithJoints', 'Host API slot 934 (2 DLLs call it). [I] Calls slot 924 for the mesh boxes, then walks the per-mesh sub table at +24 to widen a second min and max pair (a6, a7). What the second pair covers is not traced.'),
    0x63B7F0: ('FFX_ChrInstance_SetWord28', 'Host API slot 847 (14 DLLs call it). [V] Stores a2 at +28 of the object a1 when a1 is not null. What +28 means is not traced.'),
    0x63B810: ('FFX_ChrInstance_SetAlpha', 'Host API slot 833 (14 DLLs call it). [V] Writes MaterialColour (1,1,1,a) to every material, picks the fake transparent pass when a<1, unlinks all meshes at a==0 and relinks when it rises again (0x63B810). 14 DLLs.'),
    0x63B9E0: ('maybe_FFX_ChrInstance_ClearAlphaThreshold', "Host API slot 834 (2 DLLs call it). [I] Walks every mesh of a render instance and writes 0.0 to the 'AlphaThreshold' shader param."),
    0x63C9E0: ('FFX_ChrInstance_SetAddLightColour', 'Host API slot 835 (no DLL reaches it). [V] No magic DLL calls this slot. Per mesh calls sub_6A9BB0: writes AddLgtClr (r,g,b,a*0.4) and Shininess rgb (strings at 0x6A9BB0).'),
    0x63CA50: ('FFX_ChrInstance_SetMaterialAndAddLightColour', 'Host API slot 836 (no DLL reaches it). [V] No magic DLL calls this slot. Per mesh calls sub_6A9CD0: MaterialColour from the first four floats, AddLgtClr and Shininess from the last four.'),
    0x63CB00: ('FFX_ChrLight_SetLightColour', 'Host API slot 837 (no DLL reaches it). [V] No magic DLL calls this slot. Sets chr light a1 colour (sub_6AA940, index 3 is the +400 one), refreshes through sub_6AA260 and for index 3 sub_6A7550 (0x63CB00).'),
    0x63D710: ('FFX_ChrInstance_SetClipPlane', "Host API slot 930 (2 DLLs call it). [V] Writes a vec4 to the 'clipPlane' shader param of every mesh of the instance (sub_635630 walk)."),
    0x63D810: ('FFX_Chr_ApplyMagicShader', 'Host API slot 855 (4 DLLs call it). [V] Per mesh of the chr calls sub_6DFAD0, which clones the materials onto a special shader picked by mode: 0 CharacterBtmToTop, 1 MagicFile249, 2 MagicFile297 (0x6DF3D0, 0x6DF470, 0x6DF510). 4 DLLs.'),
    0x63D870: ('FFX_Chr_RestoreAllMagicShaders', "Host API slot 856 (3 DLLs call it). [V] sub_6DF620 walks the override map off_C34B4C, restores each mesh's original materials (sub_6DF6D0) and clears both maps (0x63D870). 3 DLLs."),
    0x63D8E0: ('FFX_Chr_SetMaterialTexture', 'Host API slot 906 (1 DLLs call it). [V] Per mesh calls sub_6DF950, which looks up texture a3 by name (sub_69B4B0) and binds it to material parameter a2 (0x63D8E0). 1 DLL.'),
    0x63D940: ('FFX_Chr_SetMaterialVec4', 'Host API slot 857 (4 DLLs call it). [V] Per mesh calls sub_6DFA30, which writes four floats from four pointers to the material parameter named a2 (0x63D940). 4 DLLs.'),
    0x641E60: ('FFX_ChrInstance_SetBallParams', "Host API slot 914 (1 DLLs call it). [V] Writes 'Ball_Center' (from a2 with x and y negated, w 1), 'Ball_Range' and 'Ball_Rate' shader params on every mesh of the instance."),
    0x642110: ('FFX_ChrInstance_SetVertexColorHeight', "Host API slot 818 (2 DLLs call it). [V] Walks every mesh and writes the 'VertexColorHeightNow' shader param group from the four vectors passed in."),
    # dyn geo
    0x63FC30: ('FFX_VfxModel_GetBoundsMax', 'Host API slot 891 (3 DLLs call it). [V] Returns record+164, the bounds max FFX_Vfx_CreateModel sets to +10000 (0x644DE0). 3 DLLs.'),
    0x63FC40: ('FFX_VfxModel_GetBoundsMin', 'Host API slot 890 (3 DLLs call it). [V] Returns record+152, the bounds min FFX_Vfx_CreateModel sets to -10000 (0x644DE0). 3 DLLs.'),
    0x63FC50: ('FFX_VfxModel_GetPart', 'Host API slot 887 (3 DLLs call it). [V] Returns record+148 + 108*index, one 108 byte part (0x63FC50). 3 DLLs.'),
    0x63FC70: ('FFX_VfxModel_GetPartCount', 'Host API slot 888 (no DLL reaches it). [V] No magic DLL calls this slot. Returns record+144, the part count.'),
    0x63FC90: ('FFX_VfxPart_GetColours', 'Host API slot 900 (3 DLLs call it). [V] Returns part+20, the float4 colour array (DYNAMIC_MESHES part table). 3 DLLs.'),
    0x63FCA0: ('FFX_VfxPart_GetIndices', 'Host API slot 902 (3 DLLs call it). [V] Returns part+28, the u16 index array. 3 DLLs.'),
    0x63FCB0: ('FFX_VfxPart_GetIndexCount', 'Host API slot 896 (no DLL reaches it). [V] No magic DLL calls this slot. Returns part+8, the index count.'),
    0x63FCD0: ('FFX_VfxPart_GetTexCoords', 'Host API slot 901 (2 DLLs call it). [V] Returns part+24, the float2 UV array. 2 DLLs.'),
    0x63FCE0: ('FFX_VfxPart_GetPositions', 'Host API slot 898 (3 DLLs call it). [V] Returns part+12, the float3 position array. 3 DLLs.'),
    0x63FCF0: ('FFX_VfxPart_GetVertexCount', 'Host API slot 894 (no DLL reaches it). [V] No magic DLL calls this slot. Returns part+4, the vertex count.'),
    0x63FD00: ('FFX_VfxPart_SetIndexCount', 'Host API slot 897 (no DLL reaches it). [V] No magic DLL calls this slot. Writes part+8.'),
    0x63FD10: ('FFX_VfxPart_SetVertexCount', 'Host API slot 895 (no DLL reaches it). [V] No magic DLL calls this slot. Writes part+4.'),
    0x63FD20: ('FFX_VfxPart_SetSamplerState', 'Host API slot 903 (2 DLLs call it). [V] Writes part+36, the sampler state index for sub_65E050 (DYNAMIC_MESHES part table). 2 DLLs.'),
    0x63FD30: ('FFX_VfxModel_SetPartCount', 'Host API slot 889 (no DLL reaches it). [V] No magic DLL calls this slot. Writes record+144.'),
    0x63FD50: ('FFX_VfxModel_SetZCorrect', 'Host API slot 892 (3 DLLs call it). [V] Writes record+128, the int zCrct that ClassDynamicGeometry__submit reads (0x6A4A50). 3 DLLs.'),
    0x63FDB0: ('FFX_DynGeoMem_AllocModelPart', "Host API slot 886 (8 DLLs call it). [V] Allocates one block from DynGeoMem dword_CDEDF0 for a part's positions, colours, UVs, normals and indices, and writes the pointers and counts for the current set (0x63FDB0). Name from VISUAL_CENSUS. 8 DLLs."),
    0x6453D0: ('FFX_Vfx_GetModelRecord', 'Host API slot 773 (232 DLLs call it). [V] Looks the u64 key up in registry dword_CCCA6C (sub_69F6E0) and returns geometry +36, the transform record with parts at +144/+148. 232 DLLs.'),
    0x645710: ('FFX_Vfx_SetModelLinked', 'Host API slot 884 (3 DLLs call it). [V] Looks the key up (sub_69F620) and links (sub_6A49E0) or unlinks (sub_6A2370) its mesh instance through FFX_Gfx_LinkMeshToNode. 3 DLLs.'),
    0x712BD0: ('FFX_Vfx_ReserveShpCounts', 'Host API slot 910 (1 DLLs call it). [V] Adds 6*n indices and 4*n vertices to part 0 of the model for shape a3 of the shp list a2 (0x712BD0). Used between reset and alloc in pppKeShpTail_Draw. 1 DLL.'),
    0x713290: ('FFX_Vfx_ResetModelPartCounts', "Host API slot 909 (1 DLLs call it). [V] Zeroes every part's vertex and index count and the current set's copies (+44/+48 + 28*set) (0x713290). 1 DLL."),
    0x713320: ('FFX_Vfx_ClearModelParts', "Host API slot 937 (4 DLLs call it). [V] As 0x713290 but also zeroes each part's index buffer (+28, 2 * capacity +104) (0x713320). 4 DLLs."),
    # effect objects and prims
    0x7644D0: ('FFX_PppTh_SpawnNode', 'Host API slot 677 (12 DLLs call it). [V] pppKeTh node pool: pops a node off the free list at pool+4, inits it (sub_763030), sets node flags from the list byte +4, copies two vec4s (FFX_Ppp_Vec4Copy) and links it at the head of the live list a2. Called from pppKeTh_Calc 0x736221. 12 DLLs call it from their own Th operator copies.'),
    0x7645E0: ('FFX_PppTh_DrawNodes', 'Host API slot 680 (12 DLLs call it). [V] pppKeTh node list draw: skipped on catch-up steps, builds the proj and inverse camera matrices, walks the live list calling sub_763100 per node, then builds or destroys the dynamic geometry model for the op (FFX_Vfx_BuildModelGeometry, FFX_DynGeoModel_DestroyGroupByKey). Called by pppKeTh_Draw. 12 DLLs call it.'),
    0x764800: ('FFX_PppTh_InitList', 'Host API slot 678 (10 DLLs call it). [V] Clears a pppKeTh live list head: PRX pointer 0 at +0 and flag byte +4 = 0 (0x764800). Called by pppKeTh_Con. 10 DLLs call it.'),
    0x7649A0: ('FFX_PppTh_StepNodes', 'Host API slot 679 (12 DLLs call it). [V] pppKeTh per frame step: walks the live list, refreshes the two endpoint vec4s on flagged nodes, steps each node (sub_7637F0, 3295 bytes of segment math), and returns dead nodes (flag 0x10) to the pool free list. Called by pppKeTh_Calc 0x736432. 12 DLLs call it.'),
    0x764BB0: ('FFX_PppTh_InitNodePool', 'Host API slot 676 (10 DLLs call it). [V] Builds a pppKeTh node pool: count a2 nodes of 48*(a3-1)+128 bytes each after a 16 byte header, chained as a PRX pointer free list from header+4 (0x764BB0). Called by all 37 pppKeThRes*_Con operators. 10 DLLs call it.'),
    0x767790: ('FFX_Vfx_FrameBufferPatch_Draw', "Host API slot 604 (1 DLLs call it). [V] Draws a screen space patch that samples the 'FrameBuffer' texture: projects the point, culls by screen box (magic ids 106 and 445 widen it), creates the op model with a FrameBuffer part on first use, fills 9 vertex colours, positions and UVs. Always called right after FFX_Vfx_FrameBufferPatch_Setup (pppKeShpDtt_Draw 0x755390, FFX_Opp_Kind12). 1 DLL (0271)."),
    0x768D30: ('FFX_Vfx_FrameBufferPatch_Setup', 'Host API slot 603 (1 DLLs call it). [V] Loads the emulated VU state for FFX_Vfx_FrameBufferPatch_Draw: pushes a scratch matrix, copies the camera and param block vectors into the 0xC8F7A0..0xC8F9A0 globals and the 0.7071 corner offsets. Called right before the draw in pppKeShpDtt_Draw, pppKeShpTailPht_Draw and FFX_Opp_Kind12. 1 DLL (0271).'),
    0x7EAAD0: ('FFX_Vfx_DrawScreenColorFill', 'Host API slot 352 (2 DLLs call it). [V] Full screen colour fill: rgba plus blend mode byte (bit 0x20 swaps in the alpha arg), pushes pass command 2 via FFX_GfxCtx_PushPassCommand2UnlessCatchUp, then builds a dead PS2 GS packet. Magic 386 premultiplies rgb by alpha/128 (0x7EAAD0). Callers FFX_Oef2_Op077, sub_7EADB0.'),
    0x7EB350: ('FFX_Vfx_RemoveCallbackGroup0', 'Host API slot 418 (1 DLLs call it). [V] sub_7EB4A0(id, 0): unregisters a callback from group 0, the group FFX_Vfx_EndPass runs first (0x7EC486). Group 32 twin is FFX_Vfx_RemoveEffectCallback.'),
    0x7EB370: ('FFX_Vfx_AddCallbackGroup0', 'Host API slot 417 (1 DLLs call it). [V] sub_7EB500(fn, a, b, count, 0): registers a callback in group 0, which FFX_Vfx_EndPass runs before the draw batch mark (0x7EC486). Callers FFX_RcBg_Step, maybe_FFX_Oef2_OpRegisterHdrCallback.'),
    0x7EB3A0: ('FFX_Vfx_RemoveCallbackGroup64', 'Host API slot 376 (6 DLLs call it). [V] sub_7EB4A0(id, 64): unregisters a callback from group 64, run third by FFX_Vfx_EndPass (0x7EC4CA). 6 DLLs.'),
    0x7EB3C0: ('FFX_Vfx_AddCallbackGroup64', 'Host API slot 377 (6 DLLs call it). [V] sub_7EB500(fn, a, b, count, 64): registers a callback in group 64, run third by FFX_Vfx_EndPass after the free list rebuild (0x7EC4CA). 6 DLLs.'),
    0x7EC7D0: ('FFX_Vfx_SetRebuildNodeFreeList', 'Host API slot 381 (6 DLLs call it). [V] byte_11A0067 = (a1 != 0). FFX_Vfx_EndPass tests it at 0x7EC4A4 and, when set, relinks the 4131 node free list at unk_12A89AC. Callers op_ovl_effect_free, maybe_FFX_Oef2_OpRegisterHdrCallback. 6 DLLs.'),
    0x7EC7E0: ('FFX_Vfx_DrawScreenDepthQuad', 'Host API slot 315 (10 DLLs call it). [V] Maps a PS2 24 bit Z (0 -> 1.0) to a depth, then on a rotating dynamic geometry key 1..5 (qword_C48C10) fills a full screen quad at that depth (sub_71E330) and submits it (sub_645600), or builds the model first (sub_711680). Magic 154 marks a draw batch first. Caller FFX_Oef2_Op171.'),
    0x7ED0C0: ('maybe_FFX_Vfx_BuildEmulatedVuModelPacket', 'Host API slot 242 (1 DLLs call it). [I] 31 KB software emulation of a PS2 VU model microprogram: VU registers live at 0xC8F5xx..0xC8F9xx, takes (model data, draw block) and returns the new packet cursor, which DLL 0481 stores at block +0xB4 (0x100012FF). Only one DLL calls it.'),
    0x803220: ('FFX_Oef2_CaptureChrMeshAll', 'Host API slot 704 (4 DLLs call it). [V] Snapshots the skinned vertices of every CHR mesh into buffer a1: header (chr, count, mode a3), 16 byte part records with joint matrix pointer and visible bit, then vertex data (sub_830490), mode 2 adds per vertex extras (sub_A69F10). Special CHR kinds go through sub_80F460 (0x803220). FFX_Oef2_Op137.'),
    0x803360: ('FFX_Oef2_CaptureChrMeshVisible', 'Host API slot 362 (30 DLLs call it). [V] Same capture as FFX_Oef2_CaptureChrMeshAll but skips meshes FFX_Ch_ModelGetHide reports hidden (0x803360). Callers FFX_Oef2_ExplodeModel, FFX_Oef2_Op137. 30 DLLs.'),
    0x8037C0: ('FFX_Oef2_GetChrMeshCaptureSize', "Host API slot 363 (10 DLLs call it). [V] Buffer size for the mesh capture: 16*(count+1) plus each mesh's 16 aligned vertex size, mode 2 adds 12 per 28 byte vertex, a3 limits it to visible meshes (0x8037C0). 10 DLLs."),
    0x829C60: ('FFX_Vfx_ArmAlternateTexOnce', 'Host API slot 946 (1 DLLs call it). [V] If the CHR id word is 0x1154 or the battle id is 0x024B0006, sets dword_CEC198 = 1. sub_712EC0 consumes it once to load texture 15281 instead of 15280 (0x712F36). DLL 0434.'),
    0xA5BF50: ('FFX_Vfx_DrawModelScroll', "Host API slot 867 (2 DLLs call it). [V] Creates the VFX model on first use (sub_A5C130, string 'Virtuos Error: Have not load the vfx texture (ModelScroll %s'), builds geometry, fills the prims (sub_A5C370, skipped on catch up steps when cached) and FFX_Vfx_SubmitModel, else FFX_DynGeoModel_DestroyGroupByKey. DLLs 0165, 0606."),
    # effect units and registry
    0x7FB290: ('FFX_Ch_StartChrEffects', 'Host API slot 546 (85 DLLs call it). [V] Finds the CHR in g_ffxChrShadowRegistry and, unless byte_11A0065 or entry bit 1 is set, starts link ids 0x40000400+i .. 0x40000700+i in slots 4..7 of its effect unit via sub_7FC2E0, then sets entry bit 1 (0x7FB290). Core 462 ShowSelfChrEffects calls it. 85 DLLs.'),
    0x7FBA40: ('FFX_Ch_EffectRegistrySetBit4', "Host API slot 309 (4 DLLs call it). [V] Sets bit 4 of the CHR's effect mask byte (byte_12A85B0 if nonzero else byte_12A84C0), returns 4 if it was clear. Opp kinds test that mask against obj+0xB3 to skip objects (0x811484). Core 463 HideSelfChrEffects calls it. Twin of FFX_Ch_ShadowRegistrySetBit."),
    0x7FBAB0: ('FFX_Ch_EffectRegistryClearBit4', "Host API slot 310 (4 DLLs call it). [V] Clears bit 4 of the CHR's effect mask byte (byte_12A85B0 if nonzero else byte_12A84C0), returns 4 if it was clear. Core 462 ShowSelfChrEffects and FFX_Field_ChrRegTask call it (0x7FBAB0)."),
    0x7FBB90: ('FFX_Ch_KillChrEffects', "Host API slot 695 (2 DLLs call it). [V] Clears registry entry bits 0..1 and frees the CHR's four effect links 0x40000400+i .. 0x40000700+i in effect unit 0 at once via sub_7FDE30 (0x7FBB90). Callers FFX_Ch_ReleaseShadowRegistryEntry, FFX_Oef2_Op082."),
    0x7FBCD0: ('FFX_Ch_EndChrEffects', "Host API slot 525 (86 DLLs call it). [V] Clears registry entry bits 0..1, then for each of the CHR's four link records sets 0x4000 in its unit word +176, an end request rather than a free (0x7FBCD0). Caller sub_788E60, Btl 094 ResetChrState. 86 DLLs."),
    0x7FC130: ('FFX_Ch_EffectRegistryEndFlagOverride', "Host API slot 547 (199 DLLs call it). [V] For the CHR's slot: byte_12A84C0 = override byte & 0x7F, override byte_12A85B0 = 0, returns the old override (0x7FC167). Undoes FFX_Ch_EffectRegistryBeginFlagOverride. 199 DLLs."),
    0x7FC190: ('FFX_Ch_EffectRegistryBeginFlagOverride', "Host API slot 548 (91 DLLs call it). [V] For the CHR's slot: override byte_12A85B0 = normal byte | 0x80, normal byte_12A84C0 = 0, returns the old override (0x7FC1CD). While the override is nonzero the mask setters write it instead. 91 DLLs."),
    0x7FC900: ('FFX_Effect_SetScriptSignal2', 'Host API slot 575 (4 DLLs call it). [V] byte_11A006E |= 2. FFX_Oef2_Op082 case 20 branches on it (0x814725), op_ovl_effect_free clears the byte. 4 DLLs.'),
    0x7FC910: ('FFX_Effect_ClearScriptSignal2', 'Host API slot 574 (6 DLLs call it). [V] byte_11A006E &= ~2, the clear of the signal FFX_Oef2_Op082 case 20 tests (0x814725). 6 DLLs.'),
    0x7FC990: ('FFX_Effect_SetScriptSignal1', 'Host API slot 633 (reached only through DLL thunks, 1 DLLs). [V] byte_11A006E |= 1. FFX_Oef2_Op082 case 19 branches on it (0x81471C). DLL 0393 tail calls it at 0x100024AD.'),
    0x7FC9A0: ('FFX_Effect_ClearScriptSignal1', 'Host API slot 631 (1 DLLs call it). [V] byte_11A006E &= ~1, the clear of the signal FFX_Oef2_Op082 case 19 tests (0x81471C). DLL 0393.'),
    0x7FC9B0: ('FFX_Ch_AttachChrEffect', "Host API slot 623 (13 DLLs call it). [V] Starts an effect on the CHR's unit: link id = slot + ((counter + 0x300000) << 8) with a per unit counter in byte_119FEF8, sub_7FD7F0(unit, 0, 9, id), returns the id (0x7FC9B0). Callers Btl 131 AttachChrEffect, FFX_Oef2_Op133. 13 DLLs."),
    0x7FCA40: ('FFX_Ch_ReleaseChrEffect', 'Host API slot 624 (13 DLLs call it). [V] Finds link id a1 in g_ffxEffectLinkRecords and sets 0x8000 in its unit word +176 (0x7FCA40). Pairs with FFX_Ch_AttachChrEffect, called from Btl 132 ReleaseChrEffect through sub_788E30. 13 DLLs.'),
    0x7FCCC0: ('FFX_Ch_EffectRegistryListChrs', 'Host API slot 335 (1 DLLs call it). [V] Copies every nonzero CHR pointer of the 208 registry entries (stride 32 from 0x12A22C0) into the out array and returns the count (0x7FCCC0). DLL 0519.'),
    0x7FCD10: ('FFX_Ch_EffectRegistryFindChrByKey', 'Host API slot 336 (1 DLLs call it). [V] Returns the CHR of the registry entry whose +4 key (the second arg FFX_Ch_Allocate passes to the register call, 0x8252BC) equals a1, else 0 (0x7FCD18). DLL 0519.'),
    0x7FF090: ('maybe_FFX_Effect_SetResidentEffectsEnabled', 'Host API slot 568 (4 DLLs call it). [I] Stores a1 in byte word_11A0063, maybe_g_ffxTexSkipResident = !a1, and clears (a1 != 0) or sets (a1 == 0) mask bit 2 in byte_12A84C0 for every registered CHR (0x7FF0CE). FFX_Rand_SeedCosmeticStreams does the off case at init. Callers FFX_Oef2_Op041, sub_A54760.'),
    0x7FF2B0: ('FFX_Effect_DebugSetMeterSlot', "Host API slot 722 (130 DLLs call it). [V] Debug only (g_ffxSgDebugEnabled): resets record a2 & 3 of dword_22C9760 (sub_A649C0), then stores {a2, a1} there when a2's 0xF00 priority allows (sub_A64B60). Callers FFX_MapFx_LoadParticleSet, FFX_MapFx_RegisterHeapMeter. Nothing on PC reads it. 130 DLLs."),
    # file and loader
    0x9DAFD0: ('MagicFile__setTexRemapFlag', 'Host API slot 923 (1 DLLs call it). [V] dword_193FDAC = a1. MagicFile__load and unload clear it, sub_9DA420 reads it, and FFX_MagicTex_ResolveByTex0 remaps TBP 0x3600 to 0x3601 when it is 1 and magic 539 is loaded (0x660510). Only DLL 0539 calls it.'),
    0x9DB170: ('FFX_MagicApi_GetCurrentEventId', 'Host API slot 917 (1 DLLs call it). [V] Returns g_ffxCurrentEventId.'),
    0x9DB230: ('FFX_MagicApi_MakeOpHandle64', 'Host API slot 771 (8 DLLs call it). [V] Returns (a1 << 32) + g_ffxPppCurrentOpIndex, a 64 bit handle of the current ppp operator.'),
    # gfx misc
    0x639350: ('FFX_Gfx_ChrClusterToMainWorld', 'Host API slot 846 (2 DLLs call it). [V] Calls 0x6578E0: if gfx+0xFB44 is clear, moves the chr cluster gfx+0x11E4 from world gfx+0x9C to gfx+0x7C and sets the flag. Undoes FFX_GfxCtx_PushLayerBreak. 2 DLLs.'),
    0x63DF50: ('FFX_Gfx_ChrClusterToLayerWorld', 'Host API slot 845 (4 DLLs call it). [V] Tail jump to 0x65A550: if gfx+0xFB44 is set, moves the chr cluster gfx+0x11E4 from world gfx+0x7C to gfx+0x9C and clears the flag. Mirror of 0x639350. 4 DLLs.'),
    0x641E40: ('FFX_Gfx_SetSceneClearMode', 'Host API slot 878 (3 DLLs call it). [V] Writes gfx+3 (sub_66D1B0) and byte_CCB460. gfx+3 == 1 makes FFX_Frame_RenderScene begin the scene with clear 0xA (0x6699BB), byte_CCB460 == 1 drops layer 1 VFX on some maps in FFX_Vfx_SubmitModel. 3 DLLs.'),
    0x6429F0: ('FFX_MagicApi_RenderDelegationAdd', 'Host API slot 863 (no DLL reaches it). [V] No magic DLL calls this slot. One line forwarder to RenderDelegationHelper_Add(Src, Size, a3, a4).'),
    0x642FE0: ('FFX_Gfx_SetOrthoCapture', "Host API slot 858 (9 DLLs call it). [V] Fills gfx+0xFC00..0xFC68 through sub_66D000: enable, matrix, two vec4, and a texture id printed as '%d'. sub_66B560 renders the scene with an orthographic camera from these, FFX_MagicTex_ResolveByName matches the id. 9 DLLs."),
    0x643630: ('FFX_Gfx_MulMapColorScale', "Host API slot 801 (121 DLLs call it). [V] Calls sub_66D3B0(gfx, r, g, b, a4, a5, a6, 0), which multiplies the gfx+64632 colour scale and writes 'clrScaleFactor' to the map materials. 121 DLLs."),
    0x643670: ('FFX_Gfx_SetMapColorScale', "Host API slot 933 (225 DLLs call it). [V] Calls sub_66D1D0(gfx, r, g, b, a4), which sets gfx+64632 = (r,g,b,1) and writes 'clrScaleFactor'. Callers include FFX_Oef2_Op144 and MagicFile__stop. 225 DLLs."),
    0x6436A0: ('FFX_Gfx_MulMapColorScaleAlt', 'Host API slot 841 (no DLL reaches it). [V] No magic DLL calls this slot. Same as 0x643630 with the last sub_66D3B0 flag 1.'),
    0x6437E0: ('maybe_FFX_Vfx_SetDrawMdl2Param', 'Host API slot 918 (1 DLLs call it). [I] Writes dword_CEC1A0, whose only reader is sub_641370 called from pppDrawMdl2_Draw. 1 DLL.'),
    0x6438C0: ('FFX_Gfx_SetOrthoCaptureTexId', "Host API slot 859 (4 DLLs call it). [V] sub_66E5E0: when gfx+0xFC00 is enabled, sets the capture id +0xFC64 and its '%d' string +0xFC68 (see 0x642FE0). 4 DLLs."),
    0x643B80: ('FFX_HostApi_UnusedSlot848', 'Host API slot 848 (no DLL reaches it). [V] No magic DLL calls this slot. Reads a vector at +112 of the object at g_ffxGfxCtx+220 through sub_573D00 and sub_4F93F0.'),
    0x645E60: ('FFX_MagicApi_QueueScreenFadeClear', 'Host API slot 864 (no DLL reaches it). [V] No magic DLL calls this slot. Queues FFX_ScreenFade_ClearCallback on the render delegation list dword_CCCA98 at priority a1 (2049 when a1 is -1). Exe caller FFX_Opp_Kind04.'),
    0x699080: ('FFX_HostApi_UnusedSlot876', 'Host API slot 876 (no DLL reaches it). [V] No magic DLL calls this slot. Sets byte +74692 of the object at dword_CCC8A0 to 1.'),
    # lights
    0x831950: ('FFX_Light_AllocPointLights', 'Host API slot 400 (3 DLLs call it). [V] Frees the old set, then FFX_MemAlloc(44 * count) into dword_13008CC with count in dword_13008D0 and default fields (0x831950). Sg 035 AllocPointLights calls it. 3 DLLs.'),
    0x8319B0: ('FFX_Light_SetPoint', 'Host API slot 401 (3 DLLs call it). [V] Fills point light record i: position, radius, rgb, group, falloff 2.0, enabled 1 through sub_831CA0..sub_831C60 (0x8319B0). Sg 037 SetPointLight and the pppFpPointLight ops call it. 3 DLLs.'),
    0x831A30: ('FFX_Light_FreePointLights', 'Host API slot 402 (3 DLLs call it). [V] Calls sub_831BA0: FFX_MemFree(dword_13008CC), zero the pointer and count, FFX_Ch_RequestMemoryMove (0x831BA0). Sg 036 FreePointLights calls it. 3 DLLs.'),
    0x8360F0: ('FFX_Light_GetColor', 'Host API slot 544 (30 DLLs call it). [V] Copies light colour i (0..3, 0xC49744 + 16*i) to a2 (0x8360F0). The read side of FFX_Light_SetColor. 30 DLLs.'),
    0x8361C0: ('FFX_Light_GetDir', 'Host API slot 588 (8 DLLs call it). [V] Copies light direction i (0..2: 0xC49724, 0xC49734, 0x13008E8) to a2 (0x8361C0). The read side of FFX_Light_SetDir. 8 DLLs.'),
    0x836240: ('FFX_Light_GetDirMatrix', 'Host API slot 545 (18 DLLs call it). [V] Returns &unk_1300978, the matrix FFX_Light_SetDir builds from the three directions (sub_6ED620 in 0x836430). 18 DLLs.'),
    0x836310: ('FFX_Light_SetColor', 'Host API slot 589 (6 DLLs call it). [V] Stores light colour i (0..3), rebuilds the colour matrix dword_23C4400 and pushes it via sub_63CB00 (0x836310). sub_836520, the Sg 019 SetLight path, passes rgb/255. 6 DLLs.'),
    0x836430: ('FFX_Light_SetDir', 'Host API slot 590 (6 DLLs call it). [V] Stores light direction i (0..2), rebuilds the direction matrix unk_1300978 and pushes it to FFX_ChrLight via sub_63CB50 (0x836430). sub_836520 builds the vector from yaw and pitch degrees. 6 DLLs.'),
    # magic draw
    0x9066B0: ('FFX_Magic_SetSpriteVertexUv', 'Host API slot 810 (10 DLLs call it). [V] Stores u, v at +8 of sprite corner a3 (flt_25D1708), the uv pair FFX_Magic_DrawSpriteTex0 clips (0x9064CC). 10 DLLs.'),
    0x9066D0: ('FFX_Magic_SetSpriteVertexPos', 'Host API slot 809 (10 DLLs call it). [V] Stores x, y at +0 of sprite corner a3 (flt_25D1700), the position pair FFX_Magic_DrawSpriteTex0 clips (0x9064A0). 10 DLLs, 280 sites.'),
    0x908660: ('FFX_MagicDraw_GetColorBias', 'Host API slot 555 (224 DLLs call it). [V] Returns the colour bias flt_18DF01C..028 plus 128 as four ints (0x908663), the read side of FFX_MagicDraw_SetColorBias, which stores value - 128. 224 DLLs.'),
    0x9086D0: ('FFX_MagicDraw_GetClearColor', 'Host API slot 331 (303 DLLs call it). [V] Returns clear colour bytes byte_18DF178..17B as ints, or with g_ffxMagicDrawFlags 1 and 4 set, colour * scale flt_18DF00C + bias flt_18DF01C clamped 0..255 (0x90874A). MapFx 073..075 and FFX_Gs_BeginFrameDrawEnv read it. 303 DLLs.'),
    # magic draw and map
    0x908960: ('FFX_Map_GetPartMask', 'Host API slot 557 (8 DLLs call it). [V] Returns dword_18DED0C, the map part mask that MapFx 000 (sub_88EBA0) edits one bit at a time and stores back through host 560. Also read by Oef2 op 144. ATEL_MISC_LIBRARIES.md row 0.'),
    0x908980: ('FFX_MagicDraw_GetFogColorScaled', 'Host API slot 558 (79 DLLs call it). [V] Writes the fog colour (dword_18DF140 bytes) to out, and when g_ffxMagicDrawFlags bit 2 is set scales each channel by flt_18DF00C and adds flt_18DF01C, clamped to 0..255. Its exe callers are the AtelSys MapFx 063..068 fog colour get and set syscalls.'),
    0x908BB0: ('FFX_Map_GetFogRange', 'Host API slot 559 (5 DLLs call it). [V] (near, far, limit) out. Near and far come from flt_18DF148 and flt_18DF14C scaled by flt_18DEFF8, limit from flt_18DF144, and event 224 overrides all three. Callers are the AtelSys MapFx 056..061 fog near, far and limit syscalls.'),
    0x908DF0: ('FFX_Map_GetPartMatrix', 'Host API slot 406 (1 DLLs call it). [V] (index, out): copies the 64 byte matrix of map part index from the 180 byte records at unk_18DF59C, returns -1 when index >= dword_18DF588 (the part count MapFx 101 checks). Write side is sub_90A520. Yanagi tasks bound to a map part use it (0x92FC60).'),
    0x90A2B0: ('FFX_Map_SetPartMask', 'Host API slot 560 (8 DLLs call it). [V] dword_18DED0C = a1. Write side of host 557, also called by the map region load task, Oef2 op 144 and MapFx 000 (sub_88EBA0).'),
    # ppp operators
    0x736570: ('pppKeTh_Des', 'Host API slot 83 (reached only through DLL thunks, 113 DLLs). [V] Empty Des (destructor) column of ppp operator pppKeTh, record 61 of the map and DLL op tables (refs 0xC3AEB4, 0xC3C55C) and PM row 147. 113 DLLs reach it only through their pppProg thunks.'),
    0x7372B0: ('pppNeiChrPointLight_Con', 'Host API slot 571 (reached only through DLL thunks, 3 DLLs). [V] Empty Con column of ppp operator pppNeiChrPointLight, DLL op table row 211 (ref 0xC3DCC4) and PM row 148. Reached through pppProg thunks in 3 DLLs.'),
    0x7372C0: ('pppNeiChrPointLight_Des', 'Host API slot 572 (reached only through DLL thunks, 3 DLLs). [V] Empty Des column of ppp operator pppNeiChrPointLight, DLL op table row 211 (ref 0xC3DCCC) and PM row 148. Reached through pppProg thunks in 3 DLLs.'),
    0x755000: ('pppKeShpDtt_Calc', 'Host API slot 126 (reached only through DLL thunks, 188 DLLs). [V] Empty Calc column of ppp operator pppKeShpDtt, row 45 of the map and DLL op tables (refs 0xC3AC18, 0xC3C2C0) and PM row 197. Sits right before pppKeShpDtt_Con. 188 DLLs reach it through pppProg thunks.'),
    0x7597A0: ('pppKeGrvEff_Con', 'Host API slot 107 (reached only through DLL thunks, 109 DLLs). [V] Empty Con column of ppp operator pppKeGrvEff, row 93 of the map and DLL op tables (refs 0xC3B3AC, 0xC3CA54) and PM row 46. 109 DLLs reach it through pppProg thunks.'),
    0x759D70: ('pppKeHmgEff_Con', 'Host API slot 167 (reached only through DLL thunks, 55 DLLs). [V] Empty Con column of ppp operator pppKeHmgEff, DLL op table row 170 (ref 0xC3D65C) and PM row 45. 55 DLLs reach it through pppProg thunks.'),
    0x75B680: ('pppKeAcmSolid_PostPass', 'Host API slot 422 (reached only through DLL thunks, 4 DLLs). [V] Empty PostPass column (+24) of ppp operator pppKeAcmSolid, DLL op table row 251 (ref 0xC3E300) and PM row 239. YONISHI_ENGINE.md says only pppKeAcmSolid has a PostPass, and this is it. 4 DLLs reach it through thunks.'),
    # ppp runtime
    0x717410: ('FFX_Ppp_BuildPartWorldMatrix', 'Host API slot 258 (62 DLLs call it). [V] Builds g_ffxPppPartWorldMatrix (slot 754, 0x230FFA0) from the part rotation at a1+80 (FFX_Ppp_RotMatrixXYZ), scaled per row by a1+96..+104, plus the translation. Callers include opu_part_run and FFX_Vfx_StepAll.'),
    0x7175F0: ('FFX_Ppp_EmptyHook', 'Host API slot 259 (4 DLLs call it). [V] Empty function next to FFX_Ppp_CalcPassChecked. Four DLLs call it.'),
    # sound
    0x79FD20: ('FFX_Btl_SetUnitSoundKind6Flag', 'Host API slot 528 (3 DLLs call it). [V] Writes BTLUNIT +0x40F. sub_79FE80, the unit sound handle picker, refuses sound kind 6 while it is 0. Called three times by FFX_Oef2_Op043 (a sound op). 3 DLLs call it.'),
    0x7B2B70: ('FFX_Btl_PrepareVoice', 'Host API slot 593 (2 DLLs call it). [V] If a voice id is pending (dword_112C8F0) returns -1 while the loader or streamer is busy (sub_7B2C10(1)), else FFX_Voice_Prepare(id). Returns 0 when done. 2 DLLs call it (0052, 0053).'),
    0x7B2BA0: ('FFX_Btl_StartVoice', 'Host API slot 595 (2 DLLs call it). [V] If a voice id is pending returns -1 while busy (sub_7B2C10(0)), else FFX_Voice_StartDefault and shows the pending system message dword_112C8F4 through sub_79D090. Returns 0 when done. 2 DLLs call it.'),
    0x7B2BE0: ('FFX_Btl_IsVoiceBusy', 'Host API slot 594 (2 DLLs call it). [V] 1 while FFX_Voice_IsBusy or the battle message window 7 is still up (sub_79D050), else 0. Btl 103 IsVoiceBusy and 104 WaitVoiceDone. 2 DLLs call it.'),
    0x7FFC60: ('FFX_EffectSnd_UpdateAtPos', 'Host API slot 408 (341 DLLs call it). [V] For a kind 6 sound entry: pan and volume from a 3D position (sub_7FFA90) or centre pan 64 and 127*vol/256. Type 1 SE still playing gets FFX_Snd_SetSePanById and SetSeVolumeById, type 2 voice gets FFX_Voice_SetVolume and pan (0x7FFC60). FFX_Oef2_Op043. 341 DLLs.'),
    0x7FFD20: ('FFX_EffectSnd_PlayAtPos', 'Host API slot 393 (349 DLLs call it). [V] Starts a kind 6 sound entry with pan and volume from a 3D position (sub_7FFA90) or centred. Type 1 starts the SE by id (sub_886E50), type 2 stops the voice and starts battle streaming on the matching channel (0x7FFD20). FFX_Oef2_Op043. 349 DLLs.'),
    0x7FFDE0: ('FFX_EffectSnd_FadeOut', 'Host API slot 663 (10 DLLs call it). [V] Type 1 sound entry: FFX_Snd_FadeSeVolumeById(se id, a2, 0), fading the SE to zero (0x7FFDE0). FFX_Oef2_Op043. 10 DLLs.'),
    0x7FFE10: ('FFX_EffectSnd_Play', 'Host API slot 220 (358 DLLs call it). [V] Starts a kind 6 sound entry with explicit pan a2 and volume 127*a3/256. Type 1 starts the SE by id (sub_886E50), type 2 stops the voice and starts battle streaming (0x7FFE10). FFX_Oef2_Op043 calls it with pan 64, vol 256. 358 DLLs.'),
    0x8002C0: ('FFX_SpuStream_QueueSeLoadRange', 'Host API slot 221 (369 DLLs call it). [V] Unless magic 507, for kind 6 entries a2..a3 of the unit queues each type 1 SE into the 32 entry load ring with owner mask a4 (sub_7FFEB0 -> FFX_Se_LoadSeSepProjects via sub_886F30). Returns 0 (0x8002C0). Callers FFX_Oef2_Op043, FFX_Yanagi_InitProgram. 369 DLLs.'),
    0x800330: ('FFX_SpuStream_DrainRequests', 'Host API slot 550 (100 DLLs call it). [V] Loops maybe_FFX_SpuStream_ServiceRequest until both stream requests and the queued SE loads are done (0x800330). The blocking form of the poll. 100 DLLs.'),
    0x800390: ('FFX_SpuStream_RequestStream', 'Host API slot 551 (132 DLLs call it). [V] For a type 2 (stream) sound entry, claims stream request slot a2 & 1 if free and returns 0, else -1 (0x800390). FFX_Oef2_Op043. 132 DLLs.'),
    # specular chr
    0x642E00: ('FFX_Gfx_QueueSpecularPassAkaza', 'Host API slot 905 (7 DLLs call it). [V] sub_6C5B80 on the specular chr singleton: claims a slot, mode 4 (SpecularPass_nolight_akaza1) when the last arg is 16 else 3 (nolight_akaza2), copies 4 matrices, 1 matrix, 4 floats, a vec4. Pass names at 0xC16AC4/0xC16AD4. 7 DLLs.'),
    0x642E40: ('FFX_Gfx_QueueSpecularPassAkazaLit', 'Host API slot 912 (2 DLLs call it). [V] sub_6C5C90: same slot record with mode 5 (SpecularPass_akaza, 0xC16AE4) and also sets ffxLvMatrix and ffxLcMatrix (0x6C5C90). 2 DLLs.'),
    0x643770: ('FFX_Gfx_QueueSpecularPassNoLight', 'Host API slot 879 (9 DLLs call it). [V] sub_6C5E30: finds or claims a specular chr slot with mode 0 (SpecularPass_nolight, 0xC16AA4), pushes the pass and sets the SpecularMap parameters through sub_6C6390. 9 DLLs.'),
    0x643980: ('FFX_Gfx_SetSpecularMapParams', "Host API slot 780 (no DLL reaches it). [V] No magic DLL calls this slot. Calls sub_6C6830 which writes SpecularMapSampler, SpecularMapSamplerState, SpecularPower, SpecularUvOffset and Shininess on an instance's materials."),
    # stubs
    0x62F4C0: ('maybe_FFX_DebugHook_Stub', 'Host API slot 421 (reached only through DLL thunks, 3 DLLs). [I] Empty function with 119 exe callers, mostly loaders (FFX_Ev_LoadEventPackage, FFX_Field_MapRegLoadTask). Sits between _WinMain and FFX_MemZero. The callers suggest a stripped debug or profiling hook.'),
    0x62F510: ('maybe_FFX_DebugHalt_Stub', 'Host API slot 727 (130 DLLs call it). [I] Empty function right after SG_Printf_STUB. DLLs call it with 1 right after a debug print when two values disagree (magic_0021 at 0x1000117D), so it reads as a stripped halt or assert.'),
    0x62F7B0: ('maybe_FFX_Gs_ImageSync_Stub', 'Host API slot 424 (2 DLLs call it). [I] Returns 0. Sits between the two GS LoadImage stubs (423, 425). magic_0662 calls it with (buffer, value) right after a slot 423 upload of the same buffer, and FFX_SgCapture_WriteBmp is an exe caller, so it is a stripped PS2 GS transfer step.'),
    0x7E6B20: ('maybe_FFX_Ps2_FlushCacheStub', 'Host API slot 673 (28 DLLs call it). [I] Empty function. The exe calls it once in Oef2 packet build code (sub_A6B710 0xA6BA1D) and once at 0x80FFC8, the DLLs right before they publish a built buffer pointer, which fits a PS2 cache or DMA sync left as a stub. 28 DLLs call it.'),
    0x7EAAC0: ('maybe_FFX_Vfx_AppendScreenFillPacket_stub', 'Host API slot 252 (2 DLLs call it). [I] Returns 0 (0x7EAAC0). DLLs 0445 and 0645 call it as (packet, rgba, alpha byte, 0x80) and store the result back as their packet cursor, close to what FFX_Vfx_DrawScreenColorFill takes, so a PS2 packet builder compiled out on PC.'),
    0x7EC420: ('maybe_FFX_Ps2_DmaSendPacket_stub', 'Host API slot 581 (13 DLLs call it). [I] Empty on PC. Every caller pushes (packet start, size >> 4 in qwords, flags, 0), for example pppKeShpTailPht_Draw 0x754F0C with g_ffxVfxPacketCursor and DLL 0042 with 0x1000, so a PS2 DMA kick compiled out. 13 DLLs.'),
    0x7EC430: ('maybe_FFX_Ps2_DmaSendPacketAlt_stub', 'Host API slot 563 (3 DLLs call it). [I] Empty on PC. Same (packet, qword count, 0, 0) shape as nullsub_97 in DLLs 0042, 0374, 0609, and sub_A693B0 0xA69812 pushes (unk_22FB1C8, 14, 0). A second PS2 packet send routine compiled out.'),
    0x7FCBD0: ('maybe_FFX_Effect_EmptyHook', 'Host API slot 224 (reached only through DLL thunks, 1 DLLs). [I] Empty. Only DLL 0184 reaches it, through a thunk at 0x10003230 called as (0, 0, -1) at 0x1000176F. Sits in the effect API file between op_et_prayeff_send_read_through and op_event_effect_free.'),
    0x80A460: ('maybe_FFX_Effect_PostFreeNop', 'Host API slot 817 (2 DLLs call it). [I] Empty, in the FFX_Oef2_Op144 file. DLLs 0148 and 0569 call it right after a slot 804 free at 0x10001892, 14 more reach it by thunk. A PS2 era hook compiled out.'),
    0x80A4A0: ('maybe_FFX_Effect_PostInitUnitNop', 'Host API slot 816 (reached only through DLL thunks, 19 DLLs). [I] Empty, in the FFX_Oef2_Op144 file. 19 DLLs tail jump to it after FFX_Effect_InitUnit (slot 715) set up a unit, for example magic_0148 0x10001707. A PS2 era hook compiled out.'),
    0x909D30: ('FFX_HostApi_UnusedSlot577', 'Host API slot 577 (no DLL reaches it). [V] No magic DLL calls this slot. Forwards eight args to sub_912270(&g_ffxMapWork, ...), passing the last one only in debug mode.'),
    0x909D70: ('FFX_HostApi_UnusedSlot578', 'Host API slot 578 (no DLL reaches it). [V] No magic DLL calls this slot. Forwards to sub_911FC0(&g_ffxMapWork, ...).'),
    0x9DB190: ('FFX_MagicApi_ReturnZero', 'Host API slot 301 (no DLL reaches it). [V] No magic DLL calls this slot. return 0.'),
    0x9DB1A0: ('FFX_HostApi_UnusedSlot767', 'Host API slot 767 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DB1B0: ('FFX_HostApi_UnusedSlot768', 'Host API slot 768 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DB1C0: ('FFX_HostApi_UnusedSlot805', 'Host API slot 805 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DB1D0: ('FFX_HostApi_UnusedSlot826', 'Host API slot 826 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DB1E0: ('FFX_HostApi_UnusedSlot866', 'Host API slot 866 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DB1F0: ('FFX_HostApi_UnusedSlot766', 'Host API slot 766 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DB200: ('FFX_HostApi_UnusedSlot806', 'Host API slot 806 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DB210: ('FFX_HostApi_UnusedSlot807', 'Host API slot 807 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DB220: ('FFX_HostApi_UnusedSlot851', 'Host API slot 851 (no DLL reaches it). [V] No magic DLL calls this slot. An empty function.'),
    0x9DBEA0: ('FFX_MagicApi_EmptyHook', 'Host API slot 720 (354 DLLs call it). [V] An empty function that 354 DLLs call, so a hook point that does nothing on PC.'),
    # text and sprites
    0x8AB570: ('FFX_MesWin_SetLetterboxAlpha', 'Host API slot 802 (6 DLLs call it). [V] dword_C59F38 = a1. FFX_MesWin_DrawAllWindows draws the black bars with sub_8F3A50 while it is > 0 (0x8ABD53). 6 DLLs.'),
    0x8E5CD0: ('FFX_Text_ClearScissor', 'Host API slot 814 (10 DLLs call it). [V] dword_18716D0 = 0, byte_18716D4 = 0: turns off the clip rect FFX_Text_ClipQuadToScissor tests (0x8E5A24). Callers FFX_BtlMenu_DrawHelp, rcbgGuideDraw. 10 DLLs.'),
    0x8E5F60: ('FFX_Sprite_DrawFrameBufferFull', "Host API slot 877 (no DLL reaches it). [V] No magic DLL calls this slot. Draws the 'FrameBuffer' texture as one 512x416 sprite, white, through FFX_Sprite2D_Draw (0x8E5F60)."),
    0x8E8980: ('FFX_Text_SetScissorRect', 'Host API slot 813 (10 DLLs call it). [V] Enables the 2D clip rect: dword_18716D0 = 1, rect floats at flt_18716F8..1704 that FFX_Text_ClipQuadToScissor reads (0x8E5A60). Caller FFX_BtlMenu_DrawCtbWindow. 10 DLLs.'),
    # texture and GS
    0x7E4C30: ('FFX_Tex_FlushPendingImageLoads', 'Host API slot 378 (84 DLLs call it). [V] Walks the 64 queued image entries at dword_11A01A0, issues the GS LoadImage stubs (split in 256 row strips for big images), then clears the three queue arrays and the count dword_11A04A0. On PC the GS calls are stubs. 84 DLLs call it.'),
    0x7E4E40: ('FFX_Tex_ReleaseGroup', 'Host API slot 379 (3 DLLs call it). [V] Clears every entry of the 64 group masks at dword_11A04C0 that shares a bit with a1 and trims the live count word_11A04B0 to the last used entry. Callers op_fp_effect_free, op_ovl_effect_free and others. 3 DLLs call it.'),
    0x807870: ('FFX_Oef2_ApplyChrClutHsv', "Host API slot 348 (9 DLLs call it). [V] Writes recoloured CLUTs into the CHR's texture list A copy: per entry sub_807B70 shifts the saved HSV by hue a3, sat a4, value a5 and blend a6 (hue wraps at 768), and the shade coeff at CHR +0x36C when a4 + a5 is 0. Skipped when sub_7FCC80 sees registry byte +0x17 bit 0 (0x807870). FFX_Oef2_Op040. 9 DLLs."),
    0x8079F0: ('FFX_Oef2_SaveChrClutHsv', "Host API slot 349 (9 DLLs call it). [V] Converts every CLUT colour of the CHR's texture list A (original data, sub_83C9B0) to HSV words with sub_807E90 into buffer a2 (count, per entry counts, data from +32), then sets render instance mode 1 (0x8079F0). FFX_Oef2_Op040. 9 DLLs."),
    0x807B20: ('FFX_Oef2_GetChrClutHsvSize', 'Host API slot 350 (5 DLLs call it). [V] Size of the HSV buffer FFX_Oef2_SaveChrClutHsv fills: 8 bytes per CLUT colour over all list A copy entries, 16 aligned, plus 32, or 0 (0x807B20). 5 DLLs.'),
    0x820530: ('FFX_Gs_GetDBuff', 'Host API slot 245 (39 DLLs call it). [V] Writes g_ffxMainStepCounter to *a1 when a1 is non null and returns &g_ffxGsDBuff, never null (0x820530). 16 exe callers incl. FFX_Vfx_StepAll and the ppp draw ops. 39 DLLs.'),
    0x83B650: ('maybe_FFX_Gs_GetPacketBuffer', 'Host API slot 562 (38 DLLs call it). [I] Returns off_C498DC, which holds 0x1F00000, a static packet work buffer (0x83B650). Menu and debug draws take it before building prims, and the pppDrawMdl ops read the pointer directly (0x7379C7). 38 DLLs.'),
    0x83C710: ('FFX_Ch_CreateTexListACopy', "Host API slot 345 (9 DLLs call it). [V] Frees any old copy, allocates m_texListACopy (CHR +0x704) sized for every list A CLUT, names it '%s:CLUT', fills it (sub_83CA20) and switches the CHR to it (sub_83C940). Log text 'status_stone=%d(Create)'. Counterpart of FFX_Ch_ReleaseTexListACopy (0x83C710). 9 DLLs."),
    0x83C9D0: ('FFX_Ch_GetTexListACopyData', 'Host API slot 584 (1 DLLs call it). [V] Returns dword +0 of entry a2 in m_texListACopy (CHR +0x704), the private CLUT data that FFX_Oef2_ApplyChrClutHsv writes, or 0 with no copy (0x83C9D0). DLL 0672.'),
    # vector and matrix math
    0x62FD70: ('FFX_Vec3_LerpKeepW', 'Host API slot 326 (44 DLLs call it). [V] out.xyz = a*t + b*(1-t), out.w = a.w, returns b (0x62FD70). Next to FFX_Vec3_Dot.'),
    0x6ED840: ('FFX_Mtx_TransformPointToFixed', 'Host API slot 565 (135 DLLs call it). [V] Transforms a vec3 point by a matrix through sub_705850 (w = 0). With a4 != 0 the float result is copied out, with a4 == 0 each lane is stored as int(v*16 + 0.5), the PS2 12.4 fixed point.'),
    0x75EDE0: ('FFX_Ppp_Vec4Copy', 'Host API slot 685 (2 DLLs call it). [V] Copies 16 bytes from a2 to a1, bouncing through the scratch globals at 0xC8F7A0 and 0xC8F7D0 (VU register emulation). Used by 22 ppp operators and the pppKeTh node code (0x764537). 2 DLLs call it.'),
    0x75FDA0: ('FFX_Ppp_Vec3Length', 'Host API slot 706 (2 DLLs call it). [V] Returns sqrt(x*x + y*y + z*z) of a float[3] (0x75FDA0). Callers are ppp shape tail and gravity operators. 2 DLLs call it.'),
    0x7E9A30: ('FFX_Oef2_RotTransMatrixYXZ', 'Host API slot 494 (1 DLLs call it). [V] a1 = a2 rotated about Y by a3[1] (sub_7E9670), then X by a3[0] (sub_7E9580), then Z by a3[2] (sub_7E9760), 4096 units per turn through flt_C44BF0, then a4 added to the translation row. Used by ppp model draws, rcbg FP and Oef2. 1 DLL (0361).'),
    0x7EA2D0: ('FFX_Spline_SetupCubicXyz', 'Host API slot 269 (7 DLLs call it). [V] Builds a cubic spline over n vec4 points: chord length params normalised to 0..1 into out[0..n), then sub_7E9F20 solves second derivatives per axis into out+n, +2n, +3n. Evaluator is host 270 0x7EA120. Callers FFX_Oef2_Op134, sub_813480. 7 DLLs.'),
    0x7EA700: ('FFX_Mtx_SetUniformScale', 'Host API slot 383 (11 DLLs call it). [V] Writes a 4x4 matrix with s on the diagonal of the 3x3 part and 1.0 at [15], rest zero (0x7EA700). Sits between the identity builder 0x7EA6D0 and the per axis scale 0x7EA730 in math.c. 21 exe callers, 11 DLLs.'),
    0x889B80: ('FFX_Math_DegToRadWrapped', 'Host API slot 403 (10 DLLs call it). [V] Degrees times pi/180, wrapped to [-pi, pi] with FFX_Math_WrapAngleToPiF, returned (0x889B97). 10 DLLs.'),
    0x921250: ('rcbgScene_Vec3Length', 'Host API slot 453 (12 DLLs call it). [V] sqrt(x*x + y*y + z*z) of a vec3 (0x921250). Sits in the rcbg scene file next to rcbgSceneResetShake, used by rcbgPSimCreate and rcfxSpringApply.'),
    0x9212C0: ('rcbgScene_Vec3Cross', 'Host API slot 512 (1 DLLs call it). [V] a1 = a2 x a3, the cross product (0x9212C0).'),
    0x921360: ('rcbgScene_Vec3Normalize', 'Host API slot 464 (5 DLLs call it). [V] Normalises a vec3 in place through rcbgScene_Vec3Length, zeroes all four lanes when the length is 0 (0x921360).'),
    0x93D830: ('FFX_Mtx_CopyAddTranslation', 'Host API slot 395 (4 DLLs call it). [V] Copies the 4x4 a2 into a1 and adds vec3 a3 to the translation row (0x93D830).'),
    0x9DB270: ('FFX_MagicApi_MtxMulVec4', 'Host API slot 297 (5 DLLs call it). [V] out = M * v for a 4x4 M (a2) and vec4 v (a3), staged through the scratch at 0xC8F940. 5 DLLs, among them 0515, 0611 and 0667.'),
    0x9DB490: ('FFX_MagicApi_MtxFillRows', 'Host API slot 298 (4 DLLs call it). [V] Writes the vec4 at qword_C8F508 into all four rows of a1.'),
    0x9DBAF0: ('FFX_MagicApi_Vec3NormalizeLen', 'Host API slot 358 (1 DLLs call it). [V] *a3 = |a1|, a2 = a1 / |a1| (0x9DBAF0).'),
    0x9DBB40: ('FFX_MagicApi_EffectRandFloat', 'Host API slot 241 (3 DLLs call it). [V] Mixes g_ffxEffectRandState, re-seeds the CRT with srand(state & 0x7FFFFF) at 0x9DBB40, steps the state and returns a float in [1, 2) when g_ffxEffectRandInited. A third CRT re-seed path, MAGIC_DLL.md section 5.'),
    0x9DBD90: ('FFX_MagicApi_Vec4ScaleXYZ', 'Host API slot 359 (2 DLLs call it). [V] a1.xyz = a3.xyz * a2, a1.w = a3.w (0x9DBD90).'),
    0x9DBE10: ('FFX_MagicApi_Sqrt', 'Host API slot 300 (4 DLLs call it). [V] *a2 = sqrt(*a1).'),
    # vfx model draw
    0x645600: ('FFX_Vfx_SubmitModelOrdered', 'Host API slot 870 (9 DLLs call it). [V] FFX_Vfx_SubmitModelMode(key, 0x80000000), mode 0 with the sign bit that selects the ordered list (DISPLAY_LIST section 7). 9 DLLs.'),
    0x710FD0: ('FFX_Vfx_DrawMdlOrdered', 'Host API slot 871 (8 DLLs call it). [V] Creates the Mdl model (sub_713790) and its geometry, queues the build with FFX_Vfx_QueueModelBuild (0x718610), submits with FFX_Vfx_SubmitModelOrdered. Destroys the key on failure. 8 DLLs.'),
    0x711030: ('FFX_Vfx_DrawShpOrdered', 'Host API slot 872 (no DLL reaches it). [V] No magic DLL calls this slot. Creates the Shp model (sub_715170, createVFXRenderDataBufferShp string), halves part index counts, FFX_Vfx_QueueSpriteBuild, FFX_Vfx_SubmitModelOrdered.'),
    0x7110D0: ('FFX_Vfx_DrawSingleSpriteOrdered', 'Host API slot 874 (4 DLLs call it). [V] Creates a one texture sprite model (sub_715810), divides index counts by dword_C3A484, FFX_Vfx_QueueSingleSpriteBuild (0x71C0B0), FFX_Vfx_SubmitModelOrdered. 4 DLLs.'),
    0x711180: ('FFX_Vfx_DrawMdl', 'Host API slot 820 (113 DLLs call it). [V] As 0x710FD0 but submits with FFX_Vfx_SubmitModel. The usual ppp DrawMdl path (RCFLEX.md). 113 DLLs.'),
    0x7113D0: ('FFX_Vfx_CreateShpBatchModel', 'Host API slot 907 (5 DLLs call it). [V] Creates a multi texture shape model sized for a4 copies (sub_714D40), divides index counts by 3. No build or submit, callers then reset, reserve and append per shape (pppKeShpTail_Draw). 5 DLLs.'),
    0x711460: ('FFX_Vfx_DrawShp', 'Host API slot 778 (123 DLLs call it). [V] As 0x711030 but submits with FFX_Vfx_SubmitModel. Used by pppDrawShape and opp kinds 1 and 9. 123 DLLs.'),
    0x711500: ('FFX_Vfx_DrawSingleSprite', 'Host API slot 873 (7 DLLs call it). [V] As 0x7110D0 but submits with FFX_Vfx_SubmitModel. Called by FFX_Opp_Kind10. 7 DLLs.'),
    0x7116D0: ('FFX_Vfx_DrawFrameBufferNoiseStrips', "Host API slot 860 (1 DLLs call it). [V] Model key 6 on 'D3D11FrameBuffer' (sub_716050), filled by sub_71DBD0 with rand placed strips sampling the frame buffer, tinted by four bytes, sets dword_CEC19C for FFX_Frame_RenderScene. 1 DLL."),
    0x711750: ('FFX_Vfx_DrawFrameBufferGrid', "Host API slot 862 (2 DLLs call it). [V] Pushes a frame buffer copy, creates a 33x33 grid model on 'FrameBuffer' (sub_715A40, 1089 verts), fills UVs from a2 and colour a3 (sub_71CE20), submits in mode 4 (sub_644DB0). A screen warp. 2 DLLs."),
    0x7117C0: ('maybe_FFX_Vfx_DrawFrameBufferMesh', "Host API slot 828 (2 DLLs call it). [I] Skips catch-up steps and modes above 2. Builds a triangle mesh model (sub_715AF0) textured by its own TEX0 (mode 0), 'FrameBuffer' (1) or 15040 (2), fills it through matrix a2 (sub_71CF90), submits. Role partly inferred. 2 DLLs."),
    0x711830: ('maybe_FFX_Vfx_DrawMdlImmediate', 'Host API slot 815 (10 DLLs call it). [I] Key (a1, ppp op index). Creates a model from the mesh at a2+28 (sub_715DC0, blend from a3+8) and fills it at step with sub_71D520 instead of the prim build queue, then submits. 10 DLLs.'),
    0x712690: ('FFX_Vfx_AppendTriPair', 'Host API slot 830 (1 DLLs call it). [V] Appends one triangle to each of models (a1,0) and (a1,1) from the 3 vertex records at a2+16 and a2+160, counter dword_CEC194. With a2 null it links and builds or submits both (0x712690). 1 DLL.'),
    0x7128D0: ('FFX_Vfx_AppendUntexturedQuad', 'Host API slot 832 (3 DLLs call it). [V] Appends six vertices (two triangles) to model (a1,0) from a2, counter dword_CEC194. With a2 null it links and submits (0x7128D0). Pair of 0x713160. 3 DLLs.'),
    0x712D00: ('FFX_Vfx_BoolFromInt', 'Host API slot 777 (229 DLLs call it). [V] Returns a1 != 0. The pppDrawMdl draw columns call it.'),
    0x712EC0: ('FFX_Vfx_CreateTriPairModels', 'Host API slot 829 (1 DLLs call it). [V] Creates models (a1,0) on 15280 or 15281 (dword_CEC198) and (a1,1) on 13600, sized 3*a2, allocates DynGeoMem, returns both records, zeroes dword_CEC194. a2 0 destroys both. Pair of 0x712690. 1 DLL.'),
    0x713160: ('FFX_Vfx_CreateUntexturedQuadModel', "Host API slot 831 (3 DLLs call it). [V] Creates model (a1,0) on 'NoTexture' with 6*a2 vertices and indices, allocates DynGeoMem, zeroes dword_CEC194. a2 0 destroys it. Pair of 0x7128D0. 3 DLLs."),
    0x718610: ('FFX_Vfx_QueueModelBuild', 'Host API slot 819 (121 DLLs call it). [V] Skipped on a catch-up step. Captures a model slot for the build job, sets the job built byte +193, and bakes colour scale and UV scroll. VISUAL_CENSUS names it the kind 0 prim queue producer.'),
    0x71A4E0: ('FFX_Vfx_AppendShpGeometry', "Host API slot 908 (5 DLLs call it). [V] Skips catch-up steps. Appends one shape's quads into the batched model at the current counts (+4 += 4n, +8 += 6n) and sets sort depth. pppKeShpTail_Draw calls it per segment after 0x712BD0 and the alloc. 5 DLLs."),
    0x71B8A0: ('FFX_Vfx_QueueSpriteBuild', 'Host API slot 774 (123 DLLs call it). [V] Fills kind 1 slots of the prim build queue for a shp list (run by FFX_Vfx_PrimBuild_Sprite) and sets record +193. Name from VISUAL_CENSUS. 123 DLLs.'),
    0x71C0B0: ('FFX_Vfx_QueueSingleSpriteBuild', 'Host API slot 875 (7 DLLs call it). [V] One shape version of 0x71B8A0: a kind 1 prim build slot, or for shape type 3 (a2+15) quads filled at step in view space. Called by 0x7110D0, 0x711500 and FFX_Opp_Kind10. 7 DLLs.'),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong, say less than is now known, or are positional with a digit
# ---------------------------------------------------------------------------------------------

RENAMES = {
    # ATEL helpers
    0x86AE90: ('FFX_Atel_GetPartyCharChr', 'was maybe_FFX_Atel_PartyCharHasPendingScript. Host API slot 413 (337 DLLs call it). [V] Finds the actor of party character a1 and returns its CHR (+0x9C) when attach kind +0xAA is 1, else 0 (0x86AE90). Not a pending script test. MapFx 088 and Save 053 use the result as a CHR. 337 DLLs.'),
    0x86B030: ('FFX_Atel_TestSignalFlag', 'was maybe_FFX_Atel_TestFlag. Host API slot 261 (45 DLLs call it). [V] Returns bit a1 of dword_1326B70, the per map signal mask Core 378/379 set and clear (0x86B03D). Core 376 and 377 WaitSignalFlag call it. 45 DLLs.'),
    # PRX pointer
    0xA44650: ('FFX_PrxPtr_Get', 'was maybe_FFX_PrxPtr_Get. Host API slot 941 (539 DLLs call it). [V] Returns its argument. A PS2 PRX relative pointer resolve that is the identity on PC. 539 DLLs, 961 exe xrefs. Confirms the guess.'),
    0xA44660: ('FFX_PrxPtr_SetLast', 'was maybe_FFX_PrxPtr_SetLast. Host API slot 940 (no DLL reaches it). [V] No magic DLL calls this slot. Stores its argument in dword_1A85F38. Confirms the guess.'),
    0xA446A0: ('FFX_PrxPtr_IsNull', 'was maybe_FFX_PrxPtr_IsNull. Host API slot 944 (no DLL reaches it). [V] No magic DLL calls this slot. return a1 == 0. Confirms the guess.'),
    0xA446B0: ('FFX_PrxPtr_Add', 'was maybe_FFX_PrxPtr_Add. Host API slot 943 (no DLL reaches it). [V] No magic DLL calls this slot. return a1 + a2. Confirms the guess.'),
    0xA44700: ('FFX_PrxPtr_Set', 'was maybe_FFX_PrxPtr_Set. Host API slot 942 (35 DLLs call it). [V] Returns its argument, the store side of FFX_PrxPtr_Get. Confirms the guess.'),
    # Yanagi effects
    0x9254F0: ('FFX_Yanagi_SetPppHeap', "was maybe_FFX_Yanagi_SetPppHeap. Host API slot 438 (40 DLLs call it). [V] *g_ffxPppCurrentHeap = task +16, so ppp allocations go to the task's heap. Confirms the guess. Slot 439 is a byte identical twin."),
    # battle units
    0x794990: ('FFX_Btl_GetUnitYOffset', 'was maybe_FFX_Btl_GetUnitYOffset. Host API slot 664 (116 DLLs call it). [V] Confirms the maybe_ name. Returns BTLUNIT +0x534 m_yOffset when the sleep counter +0x608 is 0 and the unit is not KO (or +0xDE7 is set), or when +0x50D is set, else 0.0. Btl 192 GetUnitYOffset wraps it. 116 DLLs call it.'),
    0x794C20: ('FFX_Btl_GetUnitCenterPos', 'was maybe_FFX_Btl_GetUnitCenterPos. Host API slot 366 (117 DLLs call it). [V] Confirms the maybe_ name. Unit position (sub_796EA0), then y -= height / 2 unless the unit is airborne (+0x505, set by Btl 265) or the whole field is (g_ffxBtlFieldAirborne), when the position already is the centre. Returns -1. 117 DLLs call it.'),
    0x7950F0: ('FFX_Btl_GetUnitHeight', 'was maybe_FFX_Btl_GetUnitHeight. Host API slot 216 (13 DLLs call it). [V] Confirms the maybe_ name. FFX_Ch_GetHeight(unit CHR) times m_scale[1] (+0x3A4), 0.0 without a CHR (0x7950F0). Used by the centre and top position getters and FFX_Btl_ResolveMoveTargetPos. 13 DLLs call it.'),
    0x7956E0: ('FFX_Btl_GetUnitPos', 'was maybe_FFX_Btl_GetUnitPos. Host API slot 217 (175 DLLs call it). [V] Confirms the maybe_ name. Ally slot units only: fills a vec4 with the unit position through sub_796EA0 (CHR position, or the parent unit bone when attached), w = 1.0. Always returns 0. 175 DLLs call it.'),
    0x795980: ('FFX_Btl_GetUnitTopPos', 'was maybe_FFX_Btl_GetUnitCenterPosAlt. Host API slot 665 (116 DLLs call it). [V] Was maybe_FFX_Btl_GetUnitCenterPosAlt. Unit position, then y -= height / 2 when airborne (+0x505 or the field flag) and y -= full height when grounded. With +Y down that is the top of the unit in both cases, where the centre getter gives the middle. 116 DLLs call it.'),
    0x7960D0: ('FFX_Btl_SetUnitMoveArcMode', 'was maybe_FFX_Btl_SetUnitFlag1056. Host API slot 539 (1 DLLs call it). [V] Was maybe_FFX_Btl_SetUnitFlag1056. Ally slot units only: writes BTLUNIT +0x420, the arc mode Btl 028 SetMoveArcMode also writes and FFX_Btl_BuildArcPath reads, returns -1. 1 DLL (0472).'),
    0x796110: ('maybe_FFX_Btl_SetUnitMotionHold', 'was maybe_FFX_Btl_SetUnitFlag1042. Host API slot 540 (2 DLLs call it). [I] Was maybe_FFX_Btl_SetUnitFlag1042. Writes BTLUNIT +0x412 (AI property 93). sub_7AA160 only picks an idle motion while it is 0, and the motion release paths clear it, so it reads as a hold on the current motion. 2 DLLs call it.'),
    # character access
    0x833C30: ('FFX_Ch_GetJointWorldMatrix', 'was maybe_FFX_Ch_GetJointWorldMatrix. Host API slot 236 (159 DLLs call it). [V] Confirmed. When out and chr->m_inUse: out = CHR world matrix (0x1D0) x joint a2 matrix (+136), translation scaled by 1000 when FFX_Mot_JointIsRemapped (0x833C30). 159 DLLs.'),
    0x833D10: ('FFX_Ch_GetJointWorldPoint', "was maybe_FFX_Ch_GetJointMatrix. Host API slot 237 (236 DLLs call it). [V] Builds the joint's world matrix like FFX_Ch_GetJointWorldMatrix, then writes a point to a4: offset a3 transformed by it (FFX_Mtx_TransformVec4), or the joint origin when a3 is null (0x833D10). Returns a point, not a matrix. 236 DLLs."),
    # chr render instance
    0x63C430: ('FFX_ChrInstance_SetMeshTransformById', 'was maybe_FFX_ChrInstance_SetBoneMatrix. Host API slot 842 (2 DLLs call it). [V] Calls FFX_Gfx_SetMeshTransform(mesh, a2) for every mesh whose id (+24) equals a3. It sets a mesh transform, not a bone matrix. Was maybe_FFX_ChrInstance_SetBoneMatrix.'),
    # dyn geo
    0x63FCC0: ('FFX_VfxPart_GetWord16', 'was __fileno. Host API slot 899 (no DLL reaches it). [V] No magic DLL calls this slot. The FLIRT match __fileno is a collision: the body is return *(a1+16), the same one line getter shape as its VfxPart neighbours 0x63FCB0 (+8) and 0x63FCD0 (+24), and the slot sits in their run. What part +16 holds is not traced.'),
    # effect units and registry
    0x7FDC80: ('FFX_Effect_DebugReloadFromHost', 'was maybe_FFX_Effect_StepAll. Host API slot 717 (130 DLLs call it). [V] Not a step. Only with g_ffxSgDebugEnabled: for each debug bit in word_2321580 it reloads unit data from a host file with sub_7E5C10 (open, read whole file, gated on byte_2322795) and re-runs FFX_Oef2_SetFromUnit and op_oef2_set (0x7FDC8D). 130 DLLs call it.'),
    # file and loader
    0x9DB250: ('FFX_MagicApi_MakeHandle64', 'was maybe_FFX_MakeHandle64. Host API slot 772 (123 DLLs call it). [V] Returns (a1 << 32) + a2. Confirms the guess. 123 DLLs.'),
    # magic draw
    0x906680: ('FFX_Magic_SetSpriteVertexColor', 'was maybe_FFX_Magic_SetTexParam. Host API slot 811 (10 DLLs call it). [V] Stores rgba ints at +16 of sprite corner a5 in the 32 byte records at 0x25D1700 that FFX_Magic_DrawSpriteTex0 hands to FFX_Sprite2D_Draw (0x906481). Not a texture parameter. 10 DLLs, 280 sites.'),
    # magic draw and map
    0x9088D0: ('FFX_MagicDraw_GetColorScale', 'was maybe_FFX_MagicDraw_GetVec4. Host API slot 556 (224 DLLs call it). [V] Copies the vec4 at flt_18DF00C out (0x9088D0). sub_908980 multiplies the fog colour bytes by this vec4 and adds the bias at flt_18DF01C, so it is the colour scale, not a generic vec4. Was a maybe_ guess.'),
    0x909FF0: ('FFX_MagicDraw_SetColorBias', 'was maybe_FFX_MagicDraw_SetColorBias. Host API slot 397 (230 DLLs call it). [V] Stores four ints minus 128 as floats in flt_18DF01C..flt_18DF028, the additive half of the colour scale and bias that sub_908980 applies. Confirms the guess.'),
    0x90A040: ('FFX_MagicDraw_SetClearColor', 'was maybe_FFX_MagicDraw_SetClearColor. Host API slot 332 (345 DLLs call it). [V] Stores RGBA bytes in byte_18DF178 and, unless magic 706 is loaded, sets the app clear colour through sub_642E90 (Phyre application +128..+136). Called by AtelSys MapFx_069_SetClearColor. Confirms the guess.'),
    0x90A270: ('FFX_MagicDraw_SetColorScale', 'was maybe_FFX_MagicDraw_SetVec4. Host API slot 353 (347 DLLs call it). [V] Copies a vec4 into flt_18DF00C, the colour scale sub_908980 multiplies by. Pair of FFX_MagicDraw_GetColorScale.'),
    # ppp runtime
    0x711F20: ('FFX_Vfx_FreePrimList', 'was maybe_FFX_Vfx_FreePrimList. Host API slot 713 (532 DLLs call it). [V] Frees every particle on the list at +48 (FFX_Ppp_FreeParticle), clears the head, frees the block at +60 from the current ppp heap, and clears g_ffxPppDrawHookTable (slot 761). Body matches the guess. Was a maybe_ guess.'),
    0x712C30: ('FFX_Vfx_AlphaFuncToBlend', 'was maybe_FFX_Vfx_AlphaFuncToBlend. Host API slot 776 (249 DLLs call it). [V] Maps the PS2 ALPHA register byte to blend flags: 0x41 -> 256, 0x42 -> 16, 0x44 -> 0, 0x46 -> 32, 0x48 -> 2, 0x88 -> 512, anything else logs and returns 0. Was a maybe_ guess.'),
    0x716710: ('FFX_Vfx_ForEachPrim', 'was maybe_FFX_Vfx_ForEachPrim. Host API slot 160 (339 DLLs call it). [V] Walks the particle list at +48 and calls a2[type] (type is byte +14 of the particle record) with (particle, a3). Body matches the guess. Was a maybe_ guess.'),
    0x716A30: ('FFX_pppMemAlloc', 'was maybe_FFX_pppMemAlloc. Host API slot 351 (4 DLLs call it). [V] FFX_PppHeap_Alloc from heap a1, falling back to heap a2 when it is live, and sets g_ffxPppAllocFailed on failure. Body matches the guess. Was a maybe_ guess.'),
    # sound
    0x8000F0: ('FFX_SpuStream_PollRequest', 'was maybe_FFX_SpuStream_PollRequest. Host API slot 549 (216 DLLs call it). [V] Confirmed. Returns 0x2000/0x1000 while stream request 0 is pending or finishing, 0x2001/0x1001 for request 1, else the queued SE load count HIWORD(dword_23C3640), 0 when idle (0x8000F0). FFX_Oef2_Op043. 216 DLLs.'),
    # texture and GS
    0x7E4E80: ('FFX_Tex_ReloadGroup', 'was maybe_op_oef2_clut_load_pal. Host API slot 380 (4 DLLs call it). [V] Was maybe_op_oef2_clut_load_pal. For each live entry whose group mask (dword_11A04C0) shares a bit with a1, calls op_oef2_texture_load(dword_11A0600[i], 1) unless byte_11A05C0[i] is set. The CLUTINFO string is only the null check log. 4 DLLs call it.'),
    # vector and matrix math
    0x6ED3A0: ('FFX_Vec4_DivScalarSafe', 'was maybe_FFX_Vec4_DivScalarSafe. Host API slot 667 (116 DLLs call it). [V] Body read and matches the guess. Was a maybe_ guess.'),
    0x6ED4F0: ('FFX_Mtx_FromRows', 'was maybe_FFX_Mtx_FromRows. Host API slot 553 (23 DLLs call it). [V] Copies four row vectors into a matrix, body read and matches the guess. Was a maybe_ guess.'),
    0x93D550: ('FFX_Mtx_RotZYX', 'was maybe_FFX_Mtx_RotZYX. Host API slot 554 (220 DLLs call it). [V] Applies sub_93D690 (Z, entries 0,1,4,5), then sub_93D620 (Y, 0,2,8,10), then sub_93D5A0 (X, 5,6,9,10) with angles a3[2], a3[1], a3[0]. Confirms the maybe_ name. 220 DLLs.'),
}

# ---------------------------------------------------------------------------------------------
# Data, the host table's global slots
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    # Oef2 interpreter
    0x119FE90: ('g_ffxBattleEffectUnit', 'Host API slot 747 (data, copied by 581 DLLs). [V] The current battle effect unit. sub_7FB250 frees the previous one (sub_7FDF70 at 0x7FB25D) and stores the new one, op_et_battle_effect_free 0x7FB170 clears it.'),
    0x11A0060: ('g_ffxEffectSysFlags', 'Host API slot 750 (data, copied by 581 DLLs). [V] 0x40 byte effect system flag block, zeroed by sub_7FF960 (0x7FF9BC), which then sets +3 = 0x101 and +6 = 1. +1 is g_ffxEffectHoldFlag (FFX_Oef2_OpEffectSync 0x8166B9), +3 is cleared by FFX_Rand_SeedCosmeticStreams.'),
    0x22FB450: ('g_ffxMagicDllSharedBlock', 'Host API slot 749 (data, copied by 581 DLLs). [M] No code in FFX.exe references it or the 0x80 bytes after it (xref scan of +0..+0x40, only the table entry 0xC6589C). A block only the DLLs use through the slot.'),
    0x2321540: ('g_ffxEffectRgSettings', 'Host API slot 746 (data, copied by 581 DLLs). [V] 0xF00 byte effect debug settings block, named after the effect.rg file it loads from. sub_A63B00 zeroes it (0xA63B26) and fills it from the PS2 dev file host0:/home/%s/ffxprog/lib/effect.rg, which does not exist on PC, so retail leaves it zero apart from word +0x44 = 512. Read by 0x7FCE83, 0x810279 and sub_7FC2E0.'),
    0x2322780: ('g_ffxEffectDebugState', 'Host API slot 748 (data, copied by 581 DLLs). [V] 0xA4 byte effect debug state, zeroed by sub_7FF960 (0x7FF979). Byte 0 is toggled by a NOT at 0x7FD0E0 and byte 0 AND byte 1 freezes effect time in opp_main (0x80BDB3, ctx+530).'),
    # 0x2332E8C: applied first as g_ffxEffectPacketCursorA by apply_datamap_names.py, the slot 742 note was added to that comment by hand.
    # 0x2332E8C: ('g_ffxEffectWorkCursor', 'Host API slot 742 (data, copied by 581 DLLs). [V] Effect work memory cursor. Reset to the base dword_2332E90 (set to 0x11A1260 by 0x7EC8A9) every step at 0x7FF73D, stored into effect unit 0 +84 before the resident Oef2 dispatch and read back after it (FFX_MagicResident_OvlStep 0x80062D, 0x800659). The end bound dword_2332E88 is base + 0xF0000. DLLs copy its value, not its address, at init.'),
    # Yanagi effects
    0x25D5A20: ('g_ffxYanagiCtx', 'Host API slot 760 (data, copied by 581 DLLs). [V] The Yanagi effect context. +4 is the effect handle FFX_Yanagi_EffectSetReady passes to FFX_Btl_EffectSetReady (0x92DFBC), +8 the Oef2 unit whose sub tables 5 and 6 hold text and sounds (0x92E689), +448 a screen fade that FFX_Yanagi_StartScreenFade starts and sub_924C70 steps, +0x198 the caster and +0x19C.. the target list.'),
    # battle units
    0x1133398: ('g_ffxMagicSpecialActorList', 'Host API slot 919 (data, copied by 581 DLLs). [V] [0] count then actor ids. sub_7A8C80 0x7A8DC7 marks a matching actor (+1011 = 1), only while magic 434 or 610 is loaded. No exe code writes it, the two DLLs fill it.'),
    # effect objects and prims
    0xCCCA58: ('g_ffxVfxDynGeoForceBlend4', 'Host API slot 850 (data, copied by 581 DLLs). [V] When 1, VFXDynamicGeometry__submit 0x6A5325 forces blend mode 4 for any geometry whose mode is not 2. No exe code writes it, so only a DLL can set it.'),
    # file and loader
    0xC64CE0: ('maybe_g_ffxMagicHostApiVersion', 'Host API slot 769 (data, copied by 581 DLLs). [I] The dword right before the table, value 1, then a zero. Nothing in FFX.exe references it except the table entry. A version or ABI marker is a guess.'),
    0x1940AC4: ('g_ffxMagicHideChrOnStop', 'Host API slot 935 (data, copied by 581 DLLs). [V] MagicFile__stop 0x9DA939 sets it to 1 when the stopped magic id is 344, 437 or 438. FFX_Ch_StepAll 0x82F403 then hides the first chr whose model is 4225, 4195, 4196, 4372 or 4235 and clears it.'),
    # ppp core
    0xC498DC: ('g_ffxSharedWorkBufPtr', 'Host API slot 759 (data, copied by 581 DLLs). [V] Holds 0x1F00000, a large work buffer also used by movie_str, Oef2 op 84 and the menus. The pppDrawMdl draws store it into their packet (pppDrawMdlBS_Draw 0x7379C7).'),
    0x230FD30: ('g_ffxPppDrawHookTable', 'Host API slot 761 (data, copied by 581 DLLs). [V] Table of draw hook callbacks. pppDrawHook_Draw 0x757398 calls entry [op byte +14] with (part, arg), and FFX_Vfx_FreePrimList zeroes it (0x711F93). No exe code fills it, so a DLL installs its own table through this slot.'),
    0x230FD50: ('g_ffxPppOwnerScale', "Host API slot 762 (data, copied by 581 DLLs). [V] vec4 scale of the effect's owner. opu_part_run 0x80B86D writes unit +0x3C times a constant, the rcbg path at 0x92F0CC writes per axis scales, and pppChrSclXYZMatrix_Calc 0x73492D reads it."),
    0x230FE20: ('g_ffxPppDrawMatrix', "Host API slot 755 (data, copied by 581 DLLs). [V] The ppp draw matrix. FFX_MapFx_ApplyViewMode 0x72A260 copies the model view result into it, and pppDrawMatrix_Draw 0x734B70 multiplies a particle's local matrix by it."),
    0x230FF20: ('g_ffxPppModelViewMatrix', 'Host API slot 757 (data, copied by 581 DLLs). [V] View x part world matrix (or the camera aligned or shared basis variant), built by FFX_MapFx_ApplyViewMode 0x72A260 and read by the pppKeDMat and pppKeZCrct draws.'),
    0x230FFA0: ('g_ffxPppPartWorldMatrix', "Host API slot 754 (data, copied by 581 DLLs). [V] The current particle's world matrix. sub_717410 builds it from the part's rotation (+80), scale (+96..+104) and translation (+64..+72), FFX_MapFx_ApplyViewMode 0x72A260 multiplies it by the view."),
    0x230FFE0: ('g_ffxPppDefaultHeap', 'Host API slot 741 (data, copied by 581 DLLs). [V] The dword the ppp heap pointer g_ffxPppCurrentHeap (0xC3A4CC) points at from load, its initial value is 0x230FFE0. FFX_Ppp_StartPart 0x7123C0 and FFX_Vfx_FreePrimList 0x711F20 allocate and free particle storage through *it. No exe code names it directly.'),
    # texture and GS
    0xC3E6C0: ('g_ffxClutSwizzleTable', 'Host API slot 765 (data, copied by 581 DLLs). [V] 32 byte PS2 CLUT index swizzle, 0..7, 16..23, 8..15, 24..31. pppKeTkFade_Draw 0x72F015 maps a palette index through it.'),
    0x11A0710: ('g_ffxGsDrawEnvPacket', 'Host API slot 763 (data, copied by 581 DLLs). [V] GS draw environment packet buffer. FFX_Gs_BuildDrawEnvPacket 0x7EB760 fills +0x10.. (GIF tag 0x8001, A+D) and FFX_Gs_SendDrawEnvPacket 0x7EB580 copies it out.'),
    # vector and matrix math
    0xC44BF0: ('g_ffxSinTable4096', 'Host API slot 751 (data, copied by 581 DLLs). [V] 4096 entry float sine table, entry 1 is 0.0015339 = sin(2pi/4096). FFX_Ppp_RotMatrixX 0x72C741 reads sin as [(a >> 4) & 0xFFF] and cos as [((a + 0x4000) >> 4) & 0xFFF], so a full turn is 0x10000.'),
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
