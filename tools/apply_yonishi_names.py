"""
Apply the yonishi effect engine names from reversing/YONISHI_ENGINE.md to the FFX.exe IDB.

The engine is two halves that live far apart:

    0x7E3000..0x81D000   Oef2, the effect script interpreter. FFX_Magic_Oef2Dispatch 0x80CC00 runs
                         256 byte objects of effect unit g_ffxEffectUnitPtrs[n] through the 256 entry
                         opcode table g_ffxOef2SubCommandTable 0xC48EC8 (FFX_Oef2_OpNNN), opp_main
                         0x80BD50 runs the per object kind table 0xC48E78 (FFX_Opp_KindNN), and the
                         integrator calls the move table 0xC492C8 (FFX_Oef2_MoveNN)
    0x70F400..0x760000   ppp (npart), the particle operator library. Each operator is a 40 byte record
                         {name, group, calc, draw, init, prepass, postpass, con, recon, des}. The
                         functions are named <operator>_<role> from the column they sit in, for example
                         pppKeShpTail_Draw. A helper only one operator calls is <operator>__f<hex>

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/funcs_ plus
hex, a positional <Module>__f<hex> name (the module part may hold digits here, because ppp operator
names do, for example pppKeThRes128x4), a positional FFX_AtelSys_<Lib>_NNN_<slot> syscall row name,
or no name at all. nullsub_ is a placeholder too, but no row here names one, because the shared
nullsubs sit in many table slots at once. A name starting maybe_ is a guess and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_yonishi_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # Hand checked names: the ppp heap, the rotation matrix builders, the Oef2 position writers
    0x72C3D0: ("FFX_PppHeap_Alloc", "(heap, size) first fit, 16 byte block header {next, prev, used}, size rounded to 16, adds to heap+12. Heap pointer is g_ffxPppCurrentHeap"),
    0x72C480: ("FFX_PppHeap_Init", "(heap, base, size) one free block and an end sentinel, host API idx 337"),
    0x72C510: ("FFX_PppHeap_Free", "(heap, ptr) clears the used word and merges with free neighbours"),
    0x72C730: ("FFX_Ppp_RotMatrixX", "one axis rotation, the last factor of every FFX_Ppp_RotMatrix* order that ends in X"),
    0x72C920: ("FFX_Ppp_RotMatrixY", "one axis rotation"),
    0x72CB10: ("FFX_Ppp_RotMatrixZ", "one axis rotation"),
    0x72C800: ("FFX_Ppp_RotMatrixXYZ", "Z then Y then X through FFX_Mtx_Mul_Api, used by pppMatrixXYZ_Calc and MapFx"),
    0x72C890: ("FFX_Ppp_RotMatrixXZY", "Y then Z then X, used by pppMatrixXZY_Calc and MapFx"),
    0x72C9F0: ("FFX_Ppp_RotMatrixYXZ", "Z then X then Y, used by pppMatrixYXZ_Calc and MapFx"),
    0x72CA80: ("FFX_Ppp_RotMatrixYZX", "X then Z then Y, used by pppMatrixYZX_Calc and MapFx"),
    0x72CBE0: ("FFX_Ppp_RotMatrixZXY", "Y then X then Z, used by pppMatrixZXY_Calc and MapFx"),
    0x72CC70: ("FFX_Ppp_RotMatrixZYX", "X then Y then Z, used by pppMatrixZYX_Calc, pppKeMatPht and MapFx"),
    0x81B2B0: ("FFX_Oef2_SetUnitOrChrPos", "(slot, vec) in battle FFX_Btl_SetUnitPos, on the field FFX_Ch_SetPos on g_ffxEffectChrSlots[slot]. Simulation visible on the field"),
    0x81B270: ("FFX_Oef2_SetUnitOrChrRot", "(slot, radians) in battle sub_795E20, on the field FFX_Ch_SetRotAndMoveDir"),

    # ppp operator record functions, named <operator>_<role> from their column in the 40 byte record.
    # Group +4, Calc +8, Draw +12, PrePass +20, PostPass +24, Con +28 (and +32 when shared), ReCon +32,
    # Des +36. Where one function serves several operators the name takes the operator whose source
    # file it sits in, and the comment lists the rest
    0x72D1B0: ("pppMatrixLoop_Calc", "ppp operator pppMatrixLoop, record +8, per particle calc"),
    0x72D390: ("pppMatrixXYZ_Calc", "ppp operator pppMatrixXYZ, record +8, per particle calc. Shared with pppMatrix"),
    0x72D570: ("pppMatrixXZY_Calc", "ppp operator pppMatrixXZY, record +8, per particle calc"),
    0x72D750: ("pppMatrixYXZ_Calc", "ppp operator pppMatrixYXZ, record +8, per particle calc"),
    0x72D930: ("pppMatrixYZX_Calc", "ppp operator pppMatrixYZX, record +8, per particle calc"),
    0x72DB10: ("pppMatrixZXY_Calc", "ppp operator pppMatrixZXY, record +8, per particle calc"),
    0x72DCF0: ("pppMatrixZYX_Calc", "ppp operator pppMatrixZYX, record +8, per particle calc"),
    0x72E730: ("pppKeMdlDtt_Con", "ppp operator pppKeMdlDtt, record +28, per particle constructor at birth"),
    0x72E760: ("pppKeMdlDtt_ReCon", "ppp operator pppKeMdlDtt, record +32, re-constructor on loop restart"),
    0x72E790: ("pppKeMdlDtt_Draw", "ppp operator pppKeMdlDtt, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x72EB20: ("pppVtMime_Draw", "ppp operator pppVtMime, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x72ED70: ("pppVtMime_Con", "ppp operator pppVtMime, record +28, per particle constructor at birth"),
    0x72EDB0: ("pppVtMime_ReCon", "ppp operator pppVtMime, record +32, re-constructor on loop restart"),
    0x72EDE0: ("pppVtMime_Des", "ppp operator pppVtMime, record +36, destructor"),
    0x72EE20: ("pppKeTkFade_Con", "ppp operator pppKeTkFade, record +28 and +32, constructor at birth and on loop restart"),
    0x72EE50: ("pppKeTkFade_Draw", "ppp operator pppKeTkFade, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x72F4B0: ("pppRandFloat_Calc", "ppp operator pppRandFloat, record +8, per particle calc"),
    0x72F680: ("pppRandUpFloat_Calc", "ppp operator pppRandUpFloat, record +8, per particle calc"),
    0x72F850: ("pppRandDownFloat_Calc", "ppp operator pppRandDownFloat, record +8, per particle calc"),
    0x72FA20: ("pppRandChar_Calc", "ppp operator pppRandChar, record +8, per particle calc"),
    0x72FBF0: ("pppRandUpChar_Calc", "ppp operator pppRandUpChar, record +8, per particle calc"),
    0x72FDC0: ("pppRandDownChar_Calc", "ppp operator pppRandDownChar, record +8, per particle calc"),
    0x72FFA0: ("pppRandShort_Calc", "ppp operator pppRandShort, record +8, per particle calc"),
    0x730170: ("pppRandUpShort_Calc", "ppp operator pppRandUpShort, record +8, per particle calc"),
    0x730340: ("pppRandDownShort_Calc", "ppp operator pppRandDownShort, record +8, per particle calc"),
    0x730520: ("pppRandInt_Calc", "ppp operator pppRandInt, record +8, per particle calc"),
    0x730700: ("pppRandUpInt_Calc", "ppp operator pppRandUpInt, record +8, per particle calc"),
    0x7308D0: ("pppRandDownInt_Calc", "ppp operator pppRandDownInt, record +8, per particle calc"),
    0x730AB0: ("pppRandFV_Calc", "ppp operator pppRandFV, record +8, per particle calc"),
    0x730CE0: ("pppRandUpFV_Calc", "ppp operator pppRandUpFV, record +8, per particle calc"),
    0x730EF0: ("pppRandDownFV_Calc", "ppp operator pppRandDownFV, record +8, per particle calc"),
    0x731100: ("pppRandIV_Calc", "ppp operator pppRandIV, record +8, per particle calc"),
    0x731350: ("pppRandUpIV_Calc", "ppp operator pppRandUpIV, record +8, per particle calc"),
    0x731560: ("pppRandDownIV_Calc", "ppp operator pppRandDownIV, record +8, per particle calc"),
    0x731780: ("pppRandCV_Calc", "ppp operator pppRandCV, record +8, per particle calc"),
    0x7319F0: ("pppRandUpCV_Calc", "ppp operator pppRandUpCV, record +8, per particle calc"),
    0x731C50: ("pppRandDownCV_Calc", "ppp operator pppRandDownCV, record +8, per particle calc"),
    0x731EC0: ("pppRandHCV_Calc", "ppp operator pppRandHCV, record +8, per particle calc"),
    0x732130: ("pppRandUpHCV_Calc", "ppp operator pppRandUpHCV, record +8, per particle calc"),
    0x732390: ("pppRandDownHCV_Calc", "ppp operator pppRandDownHCV, record +8, per particle calc"),
    0x7325F0: ("pppSRandFV_Calc", "ppp operator pppSRandFV, record +8, per particle calc"),
    0x7328B0: ("pppSRandUpFV_Calc", "ppp operator pppSRandUpFV, record +8, per particle calc"),
    0x732B40: ("pppSRandDownFV_Calc", "ppp operator pppSRandDownFV, record +8, per particle calc"),
    0x732DD0: ("pppSRandCV_Calc", "ppp operator pppSRandCV, record +8, per particle calc"),
    0x7330E0: ("pppSRandUpCV_Calc", "ppp operator pppSRandUpCV, record +8, per particle calc"),
    0x7333E0: ("pppSRandDownCV_Calc", "ppp operator pppSRandDownCV, record +8, per particle calc"),
    0x7336E0: ("pppSRandHCV_Calc", "ppp operator pppSRandHCV, record +8, per particle calc"),
    0x733A00: ("pppSRandUpHCV_Calc", "ppp operator pppSRandUpHCV, record +8, per particle calc"),
    0x733D10: ("pppSRandDownHCV_Calc", "ppp operator pppSRandDownHCV, record +8, per particle calc"),
    0x734020: ("pppKeMatSN_Calc", "ppp operator pppKeMatSN, record +8, per particle calc"),
    0x7340F0: ("pppKeMatSN_Con", "ppp operator pppKeMatSN, record +28, per particle constructor at birth"),
    0x734130: ("pppMatrixLoc_Calc", "ppp operator pppMatrixLoc, record +8, per particle calc"),
    0x734170: ("pppMatrixScl_Calc", "ppp operator pppMatrixScl, record +8, per particle calc"),
    0x7341E0: ("pppKeMatPht_Calc", "ppp operator pppKeMatPht, record +8, per particle calc"),
    0x734400: ("pppKeOfsMatXYZ_Calc", "ppp operator pppKeOfsMatXYZ, record +8, per particle calc"),
    0x734640: ("pppParMatrix_Calc", "ppp operator pppParMatrix, record +8, per particle calc"),
    0x734880: ("pppKeParMatR_Calc", "ppp operator pppKeParMatR, record +8, per particle calc"),
    0x734910: ("pppChrSclXYZMatrix_Calc", "ppp operator pppChrSclXYZMatrix, record +8, per particle calc"),
    0x7349B0: ("pppChrSclXZMatrix_Calc", "ppp operator pppChrSclXZMatrix, record +8, per particle calc"),
    0x734A30: ("pppChrSclYMatrix_Calc", "ppp operator pppChrSclYMatrix, record +8, per particle calc"),
    0x734AA0: ("pppChrYSclXYZMatrix_Calc", "ppp operator pppChrYSclXYZMatrix, record +8, per particle calc"),
    0x734AF0: ("pppChrXSclXYZMatrix_Calc", "ppp operator pppChrXSclXYZMatrix, record +8, per particle calc"),
    0x734B70: ("pppDrawMatrix_Draw", "ppp operator pppDrawMatrix, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x734BA0: ("pppDrawMatrixLoop_Draw", "ppp operator pppDrawMatrixLoop, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x734BD0: ("pppDrawMatrixFront_Draw", "ppp operator pppDrawMatrixFront, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x734C50: ("pppDrawMatrixNoRot_Draw", "ppp operator pppDrawMatrixNoRot, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x734CF0: ("pppDrawMatrixWood_Draw", "ppp operator pppDrawMatrixWood, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x734D90: ("pppDrawMatrixWood_PrePass", "ppp operator pppDrawMatrixWood, record +20, pre pass set hook"),
    0x734EB0: ("pppKeDMat_Draw", "ppp operator pppKeDMat, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x734EF0: ("pppKeDMatFr_Draw", "ppp operator pppKeDMatFr, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x734F80: ("pppKeDMatPht_Draw", "ppp operator pppKeDMatPht, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x735020: ("pppKeDMatPhtFr_Draw", "ppp operator pppKeDMatPhtFr, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x735160: ("pppKeZCrct_Draw", "ppp operator pppKeZCrct, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7354C0: ("pppKeZCrctShp_Draw", "ppp operator pppKeZCrctShp, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x735EE0: ("pppEiZCrctDisPos_Draw", "ppp operator pppEiZCrctDisPos, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x736050: ("pppKeTh_Calc", "ppp operator pppKeTh, record +8, per particle calc"),
    0x736460: ("pppKeTh_Con", "ppp operator pppKeTh, record +28, per particle constructor at birth"),
    0x7364E0: ("pppKeTh_ReCon", "ppp operator pppKeTh, record +32, re-constructor on loop restart"),
    0x736580: ("pppKeTh_Draw", "ppp operator pppKeTh, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x736690: ("pppKeThRes128_Con", "ppp operator pppKeThRes128, record +28, per particle constructor at birth"),
    0x7366C0: ("pppKeThRes128x4_Con", "ppp operator pppKeThRes128x4, record +28, per particle constructor at birth"),
    0x7366F0: ("pppKeThRes128x8_Con", "ppp operator pppKeThRes128x8, record +28, per particle constructor at birth"),
    0x736720: ("pppKeThRes16_Con", "ppp operator pppKeThRes16, record +28, per particle constructor at birth"),
    0x736750: ("pppKeThRes16x16_Con", "ppp operator pppKeThRes16x16, record +28, per particle constructor at birth"),
    0x736780: ("pppKeThRes16x24_Con", "ppp operator pppKeThRes16x24, record +28, per particle constructor at birth"),
    0x7367B0: ("pppKeThRes16x4_Con", "ppp operator pppKeThRes16x4, record +28, per particle constructor at birth"),
    0x7367E0: ("pppKeThRes16x64_Con", "ppp operator pppKeThRes16x64, record +28, per particle constructor at birth"),
    0x736810: ("pppKeThRes16x8_Con", "ppp operator pppKeThRes16x8, record +28, per particle constructor at birth"),
    0x736840: ("pppKeThRes24_Con", "ppp operator pppKeThRes24, record +28, per particle constructor at birth"),
    0x736870: ("pppKeThRes24x16_Con", "ppp operator pppKeThRes24x16, record +28, per particle constructor at birth"),
    0x7368A0: ("pppKeThRes24x4_Con", "ppp operator pppKeThRes24x4, record +28, per particle constructor at birth"),
    0x7368D0: ("pppKeThRes24x8_Con", "ppp operator pppKeThRes24x8, record +28, per particle constructor at birth"),
    0x736900: ("pppKeThRes255_Con", "ppp operator pppKeThRes255, record +28, per particle constructor at birth"),
    0x736930: ("pppKeThRes255x4_Con", "ppp operator pppKeThRes255x4, record +28, per particle constructor at birth"),
    0x736960: ("pppKeThRes32_Con", "ppp operator pppKeThRes32, record +28, per particle constructor at birth"),
    0x736990: ("pppKeThRes32x16_Con", "ppp operator pppKeThRes32x16, record +28, per particle constructor at birth"),
    0x7369C0: ("pppKeThRes32x24_Con", "ppp operator pppKeThRes32x24, record +28, per particle constructor at birth"),
    0x7369F0: ("pppKeThRes32x32_Con", "ppp operator pppKeThRes32x32, record +28, per particle constructor at birth"),
    0x736A20: ("pppKeThRes32x4_Con", "ppp operator pppKeThRes32x4, record +28, per particle constructor at birth"),
    0x736A50: ("pppKeThRes32x8_Con", "ppp operator pppKeThRes32x8, record +28, per particle constructor at birth"),
    0x736A80: ("pppKeThRes40_Con", "ppp operator pppKeThRes40, record +28, per particle constructor at birth"),
    0x736AB0: ("pppKeThRes40x16_Con", "ppp operator pppKeThRes40x16, record +28, per particle constructor at birth"),
    0x736AE0: ("pppKeThRes40x4_Con", "ppp operator pppKeThRes40x4, record +28, per particle constructor at birth"),
    0x736B10: ("pppKeThRes40x8_Con", "ppp operator pppKeThRes40x8, record +28, per particle constructor at birth"),
    0x736B40: ("pppKeThRes48_Con", "ppp operator pppKeThRes48, record +28, per particle constructor at birth"),
    0x736B70: ("pppKeThRes48x16_Con", "ppp operator pppKeThRes48x16, record +28, per particle constructor at birth"),
    0x736BA0: ("pppKeThRes48x4_Con", "ppp operator pppKeThRes48x4, record +28, per particle constructor at birth"),
    0x736BD0: ("pppKeThRes48x8_Con", "ppp operator pppKeThRes48x8, record +28, per particle constructor at birth"),
    0x736C00: ("pppKeThRes64_Con", "ppp operator pppKeThRes64, record +28, per particle constructor at birth"),
    0x736C30: ("pppKeThRes64x16_Con", "ppp operator pppKeThRes64x16, record +28, per particle constructor at birth"),
    0x736C60: ("pppKeThRes64x4_Con", "ppp operator pppKeThRes64x4, record +28, per particle constructor at birth"),
    0x736C90: ("pppKeThRes64x8_Con", "ppp operator pppKeThRes64x8, record +28, per particle constructor at birth"),
    0x736CC0: ("pppKeThRes8_Con", "ppp operator pppKeThRes8, record +28, per particle constructor at birth"),
    0x736CF0: ("pppKeThRes8x128_Con", "ppp operator pppKeThRes8x128, record +28, per particle constructor at birth"),
    0x736D20: ("pppKeThRes8x4_Con", "ppp operator pppKeThRes8x4, record +28, per particle constructor at birth"),
    0x736D50: ("pppKeThRes_Con", "ppp operator pppKeThRes, record +28, per particle constructor at birth"),
    0x736D80: ("pppKeThTp2_Calc", "ppp operator pppKeThTp2, record +8, per particle calc. Shared with pppKeThTp"),
    0x736E30: ("pppKeThTp2_Con", "ppp operator pppKeThTp2, record +28, per particle constructor at birth"),
    0x736E60: ("pppKeThTp_Con", "ppp operator pppKeThTp, record +28, per particle constructor at birth"),
    0x736E90: ("pppKeThSft_Calc", "ppp operator pppKeThSft, record +8, per particle calc"),
    0x737130: ("pppKeThSft_Con", "ppp operator pppKeThSft, record +28 and +32, constructor at birth and on loop restart"),
    0x7371C0: ("pppNeiChrPointLight_Draw", "ppp operator pppNeiChrPointLight, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x737770: ("pppDrawMdlBS_Draw", "ppp operator pppDrawMdlBS, record +12, per particle second call, skipped when prog flag +12 bit0. Shared with pppDrawMdl"),
    0x737B20: ("pppDrawMdlSemi_Draw", "ppp operator pppDrawMdlSemi, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x737F40: ("pppDrawMdlTs_Draw", "ppp operator pppDrawMdlTs, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7384F0: ("pppDrawMdlTs_Con", "ppp operator pppDrawMdlTs, record +28 and +32, constructor at birth and on loop restart"),
    0x738530: ("pppDrawMdl2_Draw", "ppp operator pppDrawMdl2, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x738A40: ("pppDrawMdlSemi2_Draw", "ppp operator pppDrawMdlSemi2, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x738EC0: ("pppDrawMdlTs2_Draw", "ppp operator pppDrawMdlTs2, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x739480: ("pppDrawMdlTs2_Con", "ppp operator pppDrawMdlTs2, record +28 and +32, constructor at birth and on loop restart"),
    0x7394C0: ("pppDrawMdl3_Draw", "ppp operator pppDrawMdl3, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x739D30: ("pppDrawMdlSemi3_Draw", "ppp operator pppDrawMdlSemi3, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73A370: ("pppDrawMdlTs3_Draw", "ppp operator pppDrawMdlTs3, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73AAC0: ("pppDrawMdlTs3_Con", "ppp operator pppDrawMdlTs3, record +28 and +32, constructor at birth and on loop restart"),
    0x73AB00: ("pppDrawMdlSea_Con", "ppp operator pppDrawMdlSea, record +28, per particle constructor at birth"),
    0x73AB40: ("pppDrawMdlSea_ReCon", "ppp operator pppDrawMdlSea, record +32, re-constructor on loop restart"),
    0x73AB70: ("pppDrawMdlSea_Des", "ppp operator pppDrawMdlSea, record +36, destructor"),
    0x73ABE0: ("pppDrawMdlSea_Draw", "ppp operator pppDrawMdlSea, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73B7B0: ("pppDrawMdlInf_Con", "ppp operator pppDrawMdlInf, record +28, per particle constructor at birth"),
    0x73B800: ("pppDrawMdlInf_ReCon", "ppp operator pppDrawMdlInf, record +32, re-constructor on loop restart"),
    0x73B830: ("pppDrawMdlInf_Des", "ppp operator pppDrawMdlInf, record +36, destructor"),
    0x73B9B0: ("pppDrawMdlInf_Draw", "ppp operator pppDrawMdlInf, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73CFB0: ("pppDrawMdlPSim_Con", "ppp operator pppDrawMdlPSim, record +28, per particle constructor at birth"),
    0x73CFE0: ("pppDrawMdlPSim_ReCon", "ppp operator pppDrawMdlPSim, record +32, re-constructor on loop restart"),
    0x73D010: ("pppDrawMdlPSim_Des", "ppp operator pppDrawMdlPSim, record +36, destructor"),
    0x73D070: ("pppDrawMdlPSim_Draw", "ppp operator pppDrawMdlPSim, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73D8C0: ("pppDrawMdlCamera_Con", "ppp operator pppDrawMdlCamera, record +28, per particle constructor at birth"),
    0x73D8F0: ("pppDrawMdlCamera_ReCon", "ppp operator pppDrawMdlCamera, record +32, re-constructor on loop restart"),
    0x73D930: ("pppDrawMdlCamera_Draw", "ppp operator pppDrawMdlCamera, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73E240: ("pppDrawMdlLoop_Con", "ppp operator pppDrawMdlLoop, record +28, per particle constructor at birth"),
    0x73E270: ("pppDrawMdlLoop_ReCon", "ppp operator pppDrawMdlLoop, record +32, re-constructor on loop restart"),
    0x73E2B0: ("pppDrawMdlLoop_Draw", "ppp operator pppDrawMdlLoop, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73E9D0: ("pppDrawMdlLoopZ_Con", "ppp operator pppDrawMdlLoopZ, record +28, per particle constructor at birth"),
    0x73EA00: ("pppDrawMdlLoopZ_ReCon", "ppp operator pppDrawMdlLoopZ, record +32, re-constructor on loop restart"),
    0x73EA40: ("pppDrawMdlLoopZ_Draw", "ppp operator pppDrawMdlLoopZ, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73F1C0: ("pppDrawMdlLoopDisPos_Con", "ppp operator pppDrawMdlLoopDisPos, record +28, per particle constructor at birth"),
    0x73F1F0: ("pppDrawMdlLoopDisPos_ReCon", "ppp operator pppDrawMdlLoopDisPos, record +32, re-constructor on loop restart"),
    0x73F230: ("pppDrawMdlLoopDisPos_Draw", "ppp operator pppDrawMdlLoopDisPos, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x73F9E0: ("pppDrawMdlCameraLoop_Con", "ppp operator pppDrawMdlCameraLoop, record +28, per particle constructor at birth"),
    0x73FA10: ("pppDrawMdlCameraLoop_ReCon", "ppp operator pppDrawMdlCameraLoop, record +32, re-constructor on loop restart"),
    0x73FA50: ("pppDrawMdlCameraLoop_Draw", "ppp operator pppDrawMdlCameraLoop, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7403F0: ("pppDrawMdlRev_Con", "ppp operator pppDrawMdlRev, record +28, per particle constructor at birth"),
    0x740410: ("pppDrawMdlRev_Draw", "ppp operator pppDrawMdlRev, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7409E0: ("pppDrawShape_Group", "ppp operator pppDrawShape, record +4, runs the whole group"),
    0x740EB0: ("pppDrawShape_Con", "ppp operator pppDrawShape, record +28 and +32, constructor at birth and on loop restart"),
    0x740ED0: ("pppDrawShapeX_Group", "ppp operator pppDrawShapeX, record +4, runs the whole group"),
    0x741450: ("pppDrawShapeX_Con", "ppp operator pppDrawShapeX, record +28 and +32, constructor at birth and on loop restart"),
    0x741570: ("pppDrawShapeRev_Con", "ppp operator pppDrawShapeRev, record +28 and +32, constructor at birth and on loop restart"),
    0x7415A0: ("pppDrawShapeRev_Draw", "ppp operator pppDrawShapeRev, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x742B50: ("pppDrawShapeField_Con", "ppp operator pppDrawShapeField, record +28, per particle constructor at birth"),
    0x742BD0: ("pppDrawShapeField_Des", "ppp operator pppDrawShapeField, record +36, destructor"),
    0x742CC0: ("pppDrawShapeField_Draw", "ppp operator pppDrawShapeField, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x743F00: ("pppDrawShapeFieldRev_Con", "ppp operator pppDrawShapeFieldRev, record +28, per particle constructor at birth"),
    0x743F80: ("pppDrawShapeFieldRev_Des", "ppp operator pppDrawShapeFieldRev, record +36, destructor"),
    0x744040: ("pppDrawShapeFieldRev_Draw", "ppp operator pppDrawShapeFieldRev, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7450A0: ("pppDrawShapeFieldSpd_Con", "ppp operator pppDrawShapeFieldSpd, record +28, per particle constructor at birth"),
    0x745130: ("pppDrawShapeFieldSpd_Des", "ppp operator pppDrawShapeFieldSpd, record +36, destructor"),
    0x745210: ("pppDrawShapeFieldSpd_Draw", "ppp operator pppDrawShapeFieldSpd, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x746200: ("pppDrawShapeFieldGlobal_Con", "ppp operator pppDrawShapeFieldGlobal, record +28, per particle constructor at birth"),
    0x746280: ("pppDrawShapeFieldGlobal_Des", "ppp operator pppDrawShapeFieldGlobal, record +36, destructor"),
    0x746320: ("pppDrawShapeFieldGlobal_Draw", "ppp operator pppDrawShapeFieldGlobal, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x747410: ("pppDrawShapeCamera_Con", "ppp operator pppDrawShapeCamera, record +28, per particle constructor at birth"),
    0x747490: ("pppDrawShapeCamera_Des", "ppp operator pppDrawShapeCamera, record +36, destructor"),
    0x747540: ("pppDrawShapeCamera_Draw", "ppp operator pppDrawShapeCamera, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x748660: ("pppDrawShapeCameraDisPos_Con", "ppp operator pppDrawShapeCameraDisPos, record +28, per particle constructor at birth"),
    0x7486E0: ("pppDrawShapeCameraDisPos_Des", "ppp operator pppDrawShapeCameraDisPos, record +36, destructor"),
    0x748790: ("pppDrawShapeCameraDisPos_Draw", "ppp operator pppDrawShapeCameraDisPos, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7490D0: ("pppKeMdlBmp_Draw", "ppp operator pppKeMdlBmp, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7497E0: ("pppKeMdlTfd_Con", "ppp operator pppKeMdlTfd, record +28 and +32, constructor at birth and on loop restart"),
    0x749810: ("pppKeMdlTfd_Draw", "ppp operator pppKeMdlTfd, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7498F0: ("pppKeMdlTfdUv_Con", "ppp operator pppKeMdlTfdUv, record +28 and +32, constructor at birth and on loop restart"),
    0x749950: ("pppKeMdlTfdUv_Draw", "ppp operator pppKeMdlTfdUv, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x749B10: ("pppKeMdlTfd2_Con", "ppp operator pppKeMdlTfd2, record +28 and +32, constructor at birth and on loop restart"),
    0x749B40: ("pppKeMdlTfd2_Draw", "ppp operator pppKeMdlTfd2, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x749C30: ("pppKeMdlTfdUv2_Con", "ppp operator pppKeMdlTfdUv2, record +28 and +32, constructor at birth and on loop restart"),
    0x749C90: ("pppKeMdlTfdUv2_Draw", "ppp operator pppKeMdlTfdUv2, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x749E60: ("pppKeMdlTfd3_Con", "ppp operator pppKeMdlTfd3, record +28 and +32, constructor at birth and on loop restart"),
    0x749E90: ("pppKeMdlTfd3_Draw", "ppp operator pppKeMdlTfd3, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x749F90: ("pppKeMdlTfdUv3_Con", "ppp operator pppKeMdlTfdUv3, record +28 and +32, constructor at birth and on loop restart"),
    0x749FF0: ("pppKeMdlTfdUv3_Draw", "ppp operator pppKeMdlTfdUv3, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x74A1E0: ("pppNeiDrawMdlPointLight_Draw", "ppp operator pppNeiDrawMdlPointLight, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x74A850: ("pppNeiDrawMdlSemiPointLight_Draw", "ppp operator pppNeiDrawMdlSemiPointLight, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x74AE70: ("pppNeiDrawMdlTsPointLight_Draw", "ppp operator pppNeiDrawMdlTsPointLight, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x74B500: ("pppNeiDrawMdlTsPointLight_Con", "ppp operator pppNeiDrawMdlTsPointLight, record +28 and +32, constructor at birth and on loop restart"),
    0x74B6A0: ("pppNeiDrawShapePointLight_Draw", "ppp operator pppNeiDrawShapePointLight, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x74B9A0: ("pppNeiDrawShapePointLight_Con", "ppp operator pppNeiDrawShapePointLight, record +28 and +32, constructor at birth and on loop restart"),
    0x74C490: ("pppKeShpTail_Calc", "ppp operator pppKeShpTail, record +8, per particle calc"),
    0x74C540: ("pppKeShpTail_Con", "ppp operator pppKeShpTail, record +28, per particle constructor at birth"),
    0x74C570: ("pppKeShpTail_Draw", "ppp operator pppKeShpTail, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x74D490: ("pppKeShpTailX_Calc", "ppp operator pppKeShpTailX, record +8, per particle calc"),
    0x74D540: ("pppKeShpTailX_Con", "ppp operator pppKeShpTailX, record +28, per particle constructor at birth"),
    0x74D570: ("pppKeShpTailX_Draw", "ppp operator pppKeShpTailX, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x74E490: ("pppKeShpTail2_Calc", "ppp operator pppKeShpTail2, record +8, per particle calc"),
    0x74E540: ("pppKeShpTail2_Con", "ppp operator pppKeShpTail2, record +28, per particle constructor at birth"),
    0x74E570: ("pppKeShpTail2_Draw", "ppp operator pppKeShpTail2, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x74F4D0: ("pppKeShpTail2X_Calc", "ppp operator pppKeShpTail2X, record +8, per particle calc"),
    0x74F580: ("pppKeShpTail2X_Con", "ppp operator pppKeShpTail2X, record +28, per particle constructor at birth"),
    0x74F5B0: ("pppKeShpTail2X_Draw", "ppp operator pppKeShpTail2X, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7502C0: ("pppKeShpTail3_Calc", "ppp operator pppKeShpTail3, record +8, per particle calc"),
    0x750510: ("pppKeShpTail3_Con", "ppp operator pppKeShpTail3, record +28, per particle constructor at birth"),
    0x7505A0: ("pppKeShpTail3_ReCon", "ppp operator pppKeShpTail3, record +32, re-constructor on loop restart"),
    0x750620: ("pppKeShpTail3_Draw", "ppp operator pppKeShpTail3, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x751CA0: ("pppKeShpTail3X_Calc", "ppp operator pppKeShpTail3X, record +8, per particle calc"),
    0x751EB0: ("pppKeShpTail3X_Con", "ppp operator pppKeShpTail3X, record +28, per particle constructor at birth"),
    0x751F40: ("pppKeShpTail3X_ReCon", "ppp operator pppKeShpTail3X, record +32, re-constructor on loop restart"),
    0x751FC0: ("pppKeShpTail3X_Draw", "ppp operator pppKeShpTail3X, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x753F10: ("pppKeShpTailPht_Calc", "ppp operator pppKeShpTailPht, record +8, per particle calc"),
    0x754180: ("pppKeShpTailPht_Con", "ppp operator pppKeShpTailPht, record +28, per particle constructor at birth"),
    0x754260: ("pppKeShpTailPht_ReCon", "ppp operator pppKeShpTailPht, record +32, re-constructor on loop restart"),
    0x754300: ("pppKeShpTailPht_Draw", "ppp operator pppKeShpTailPht, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x754FC0: ("pppKeShpTailLc_Con", "ppp operator pppKeShpTailLc, record +28, per particle constructor at birth"),
    0x755010: ("pppKeShpDtt_Con", "ppp operator pppKeShpDtt, record +28 and +32, constructor at birth and on loop restart"),
    0x755040: ("pppKeShpDtt_Draw", "ppp operator pppKeShpDtt, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x756240: ("pppDrawRain_Con", "ppp operator pppDrawRain, record +28, per particle constructor at birth"),
    0x756270: ("pppDrawRain_Des", "ppp operator pppDrawRain, record +36, destructor"),
    0x7562C0: ("pppDrawRain_Draw", "ppp operator pppDrawRain, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7566A0: ("pppDrawSprite_Con", "ppp operator pppDrawSprite, record +28, per particle constructor at birth"),
    0x7566E0: ("pppDrawSprite_Draw", "ppp operator pppDrawSprite, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x757210: ("pppDrawFilter_Con", "ppp operator pppDrawFilter, record +28, per particle constructor at birth"),
    0x757250: ("pppDrawFilter_Des", "ppp operator pppDrawFilter, record +36, destructor"),
    0x757280: ("pppDrawFilter_Draw", "ppp operator pppDrawFilter, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x757370: ("pppDrawHook_Draw", "ppp operator pppDrawHook, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x7573C0: ("pppPointAp_Calc", "ppp operator pppPointAp, record +8, per particle calc"),
    0x757470: ("pppPointAp_Con", "ppp operator pppPointAp, record +28, per particle constructor at birth"),
    0x757490: ("pppPointRAp_Calc", "ppp operator pppPointRAp, record +8, per particle calc"),
    0x757750: ("pppPointRAp_Con", "ppp operator pppPointRAp, record +28, per particle constructor at birth"),
    0x757840: ("pppVertexAp_Calc", "ppp operator pppVertexAp, record +8, per particle calc"),
    0x757B30: ("pppVertexAp_Con", "ppp operator pppVertexAp, record +28, per particle constructor at birth"),
    0x757C30: ("pppVertexApDisPos_Calc", "ppp operator pppVertexApDisPos, record +8, per particle calc"),
    0x757E90: ("pppVertexApDisPos_Con", "ppp operator pppVertexApDisPos, record +28, per particle constructor at birth"),
    0x757FF0: ("pppVertexApLc_Calc", "ppp operator pppVertexApLc, record +8, per particle calc"),
    0x758250: ("pppVertexApLc_Con", "ppp operator pppVertexApLc, record +28 and +32, constructor at birth and on loop restart"),
    0x7582E0: ("pppVertexApAt_Calc", "ppp operator pppVertexApAt, record +8, per particle calc"),
    0x7584B0: ("pppVertexApAt_Con", "ppp operator pppVertexApAt, record +28 and +32, constructor at birth and on loop restart"),
    0x7584D0: ("pppKeBornRnd_Calc", "ppp operator pppKeBornRnd, record +8, per particle calc"),
    0x758790: ("pppKeBornRnd_Con", "ppp operator pppKeBornRnd, record +28, per particle constructor at birth"),
    0x7587B0: ("pppKeBornRnd2_Calc", "ppp operator pppKeBornRnd2, record +8, per particle calc"),
    0x758820: ("pppKeBornRnd2_Con", "ppp operator pppKeBornRnd2, record +28, per particle constructor at birth"),
    0x758960: ("pppKeBornRnd3_Calc", "ppp operator pppKeBornRnd3, record +8, per particle calc"),
    0x7589D0: ("pppKeBornRnd3_Con", "ppp operator pppKeBornRnd3, record +28, per particle constructor at birth"),
    0x758AE0: ("pppKeBornRnd4_Calc", "ppp operator pppKeBornRnd4, record +8, per particle calc"),
    0x758DD0: ("pppKeBornRnd4_Con", "ppp operator pppKeBornRnd4, record +28, per particle constructor at birth"),
    0x758F20: ("pppKeBornRnd5_Calc", "ppp operator pppKeBornRnd5, record +8, per particle calc"),
    0x758F90: ("pppKeBornRnd5_Con", "ppp operator pppKeBornRnd5, record +28, per particle constructor at birth"),
    0x759200: ("pppKeBornRnd6_Calc", "ppp operator pppKeBornRnd6, record +8, per particle calc"),
    0x759270: ("pppKeBornRnd6_Con", "ppp operator pppKeBornRnd6, record +28, per particle constructor at birth"),
    0x7595E0: ("pppKeBornPtCmpl_Calc", "ppp operator pppKeBornPtCmpl, record +8, per particle calc"),
    0x759620: ("pppKeBornPtCmpl_Con", "ppp operator pppKeBornPtCmpl, record +28, per particle constructor at birth"),
    0x759650: ("pppKeGrvEff_Calc", "ppp operator pppKeGrvEff, record +8, per particle calc"),
    0x7597B0: ("pppKeGrvTgt_Calc", "ppp operator pppKeGrvTgt, record +8, per particle calc"),
    0x759800: ("pppKeGrvTgt_Con", "ppp operator pppKeGrvTgt, record +28, per particle constructor at birth"),
    0x759830: ("pppKeHitBall_Calc", "ppp operator pppKeHitBall, record +8, per particle calc"),
    0x759890: ("pppKeHitBorn_Calc", "ppp operator pppKeHitBorn, record +8, per particle calc"),
    0x759990: ("pppKeHitBorn_Con", "ppp operator pppKeHitBorn, record +28, per particle constructor at birth"),
    0x7599B0: ("pppKeHitChkPxB_Calc", "ppp operator pppKeHitChkPxB, record +8, per particle calc"),
    0x759BC0: ("pppKeHitChkPxB_Con", "ppp operator pppKeHitChkPxB, record +28, per particle constructor at birth"),
    0x759BF0: ("pppKeHmgEff_Calc", "ppp operator pppKeHmgEff, record +8, per particle calc"),
    0x759D80: ("pppKeThHitBorn_Calc", "ppp operator pppKeThHitBorn, record +8, per particle calc"),
    0x759F10: ("pppKeThHitBorn_Con", "ppp operator pppKeThHitBorn, record +28, per particle constructor at birth"),
    0x759F30: ("pppKeLnsLpSft_Calc", "ppp operator pppKeLnsLpSft, record +8, per particle calc"),
    0x759FB0: ("pppKeLnsLpSft_Con", "ppp operator pppKeLnsLpSft, record +28 and +32, constructor at birth and on loop restart"),
    0x759FD0: ("pppKeLnsArnd_Con", "ppp operator pppKeLnsArnd, record +28, per particle constructor at birth"),
    0x759FF0: ("pppKeLnsArnd_Draw", "ppp operator pppKeLnsArnd, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75A0F0: ("pppKeLnsArndT_Con", "ppp operator pppKeLnsArndT, record +28, per particle constructor at birth"),
    0x75A140: ("pppKeLnsArndT_Des", "ppp operator pppKeLnsArndT, record +36, destructor"),
    0x75A190: ("pppKeLnsArndT_Draw", "ppp operator pppKeLnsArndT, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75A370: ("pppKeLnsClm_Con", "ppp operator pppKeLnsClm, record +28, per particle constructor at birth"),
    0x75A3A0: ("pppKeLnsClm_Draw", "ppp operator pppKeLnsClm, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75A4A0: ("pppKeLnsClmT_Con", "ppp operator pppKeLnsClmT, record +28, per particle constructor at birth"),
    0x75A4F0: ("pppKeLnsClmT_Des", "ppp operator pppKeLnsClmT, record +36, destructor"),
    0x75A540: ("pppKeLnsClmT_Draw", "ppp operator pppKeLnsClmT, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75A740: ("pppKeLnsCrn_Con", "ppp operator pppKeLnsCrn, record +28, per particle constructor at birth"),
    0x75A760: ("pppKeLnsCrn_Draw", "ppp operator pppKeLnsCrn, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75A870: ("pppKeLnsCrnT_Con", "ppp operator pppKeLnsCrnT, record +28, per particle constructor at birth"),
    0x75A8B0: ("pppKeLnsCrnT_Des", "ppp operator pppKeLnsCrnT, record +36, destructor"),
    0x75A900: ("pppKeLnsCrnT_Draw", "ppp operator pppKeLnsCrnT, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75AB30: ("pppKeLnsFls_Con", "ppp operator pppKeLnsFls, record +28, per particle constructor at birth"),
    0x75AB50: ("pppKeLnsFls_Draw", "ppp operator pppKeLnsFls, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75AC60: ("pppKeLnsFlsT_Con", "ppp operator pppKeLnsFlsT, record +28, per particle constructor at birth"),
    0x75ACB0: ("pppKeLnsFlsT_Des", "ppp operator pppKeLnsFlsT, record +36, destructor"),
    0x75AD00: ("pppKeLnsFlsT_Draw", "ppp operator pppKeLnsFlsT, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75AF30: ("pppKeLnsLp_Con", "ppp operator pppKeLnsLp, record +28, per particle constructor at birth"),
    0x75AF60: ("pppKeLnsLp_ReCon", "ppp operator pppKeLnsLp, record +32, re-constructor on loop restart"),
    0x75AF80: ("pppKeLnsLp_Draw", "ppp operator pppKeLnsLp, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75B090: ("pppKeLnsLpT_Con", "ppp operator pppKeLnsLpT, record +28, per particle constructor at birth"),
    0x75B110: ("pppKeLnsLpT_ReCon", "ppp operator pppKeLnsLpT, record +32, re-constructor on loop restart"),
    0x75B130: ("pppKeLnsLpT_Des", "ppp operator pppKeLnsLpT, record +36, destructor"),
    0x75B160: ("pppKeLnsLpT_Draw", "ppp operator pppKeLnsLpT, record +12, per particle second call, skipped when prog flag +12 bit0"),
    0x75B690: ("pppKeAcmCic_Calc", "ppp operator pppKeAcmCic, record +8, per particle calc"),
    0x75B740: ("pppAccele_Calc", "ppp operator pppAccele, record +8, per particle calc"),
    0x75B810: ("pppAccele_Con", "ppp operator pppAccele, record +28 and +32, constructor at birth and on loop restart"),
    0x75B850: ("pppAngAccele_Calc", "ppp operator pppAngAccele, record +8, per particle calc"),
    0x75B8C0: ("pppAngAccele_Con", "ppp operator pppAngAccele, record +28 and +32, constructor at birth and on loop restart"),
    0x75B900: ("pppSclAccele_Calc", "ppp operator pppSclAccele, record +8, per particle calc"),
    0x75B9D0: ("pppSclAccele_Con", "ppp operator pppSclAccele, record +28 and +32, constructor at birth and on loop restart"),
    0x75BA40: ("pppColAccele_Calc", "ppp operator pppColAccele, record +8, per particle calc"),
    0x75BAC0: ("pppColAccele_Con", "ppp operator pppColAccele, record +28 and +32, constructor at birth and on loop restart"),
    0x75BB30: ("pppEiWfacc_Calc", "ppp operator pppEiWfacc, record +8, per particle calc"),
    0x75BDE0: ("pppMove_Calc", "ppp operator pppMove, record +8, per particle calc"),
    0x75BEB0: ("pppMove_Con", "ppp operator pppMove, record +28 and +32, constructor at birth and on loop restart"),
    0x75BEF0: ("pppAngMove_Calc", "ppp operator pppAngMove, record +8, per particle calc"),
    0x75BF60: ("pppAngMove_Con", "ppp operator pppAngMove, record +28 and +32, constructor at birth and on loop restart"),
    0x75BFA0: ("pppSclMove_Calc", "ppp operator pppSclMove, record +8, per particle calc"),
    0x75C070: ("pppSclMove_Con", "ppp operator pppSclMove, record +28 and +32, constructor at birth and on loop restart"),
    0x75C0B0: ("pppMoveLoop_Calc", "ppp operator pppMoveLoop, record +8, per particle calc"),
    0x75C150: ("pppMoveLoop_Con", "ppp operator pppMoveLoop, record +28, per particle constructor at birth"),
    0x75C180: ("pppMoveLoop_ReCon", "ppp operator pppMoveLoop, record +32, re-constructor on loop restart"),
    0x75C1B0: ("pppAngMoveLoop_Calc", "ppp operator pppAngMoveLoop, record +8, per particle calc"),
    0x75C210: ("pppAngMoveLoop_Con", "ppp operator pppAngMoveLoop, record +28, per particle constructor at birth"),
    0x75C250: ("pppAngMoveLoop_ReCon", "ppp operator pppAngMoveLoop, record +32, re-constructor on loop restart"),
    0x75C290: ("pppSclMoveLoop_Calc", "ppp operator pppSclMoveLoop, record +8, per particle calc"),
    0x75C330: ("pppSclMoveLoop_Con", "ppp operator pppSclMoveLoop, record +28, per particle constructor at birth"),
    0x75C360: ("pppSclMoveLoop_ReCon", "ppp operator pppSclMoveLoop, record +32, re-constructor on loop restart"),
    0x75C390: ("pppColMove_Calc", "ppp operator pppColMove, record +8, per particle calc"),
    0x75C410: ("pppColMove_Con", "ppp operator pppColMove, record +28 and +32, constructor at birth and on loop restart"),
    0x75C450: ("pppPoint_Calc", "ppp operator pppPoint, record +8, per particle calc"),
    0x75C4C0: ("pppPoint_Con", "ppp operator pppPoint, record +28 and +32, constructor at birth and on loop restart"),
    0x75C500: ("pppPointLoop_Calc", "ppp operator pppPointLoop, record +8, per particle calc"),
    0x75C5A0: ("pppPointLoop_Con", "ppp operator pppPointLoop, record +28, per particle constructor at birth"),
    0x75C5D0: ("pppPointLoop_ReCon", "ppp operator pppPointLoop, record +32, re-constructor on loop restart"),
    0x75CD30: ("pppKeOfsPt_Calc", "ppp operator pppKeOfsPt, record +8, per particle calc"),
    0x75CDF0: ("pppKeOfsPt_Con", "ppp operator pppKeOfsPt, record +28 and +32, constructor at birth and on loop restart"),
    0x75CE30: ("pppAngle_Calc", "ppp operator pppAngle, record +8, per particle calc"),
    0x75CE80: ("pppAngle_Con", "ppp operator pppAngle, record +28 and +32, constructor at birth and on loop restart"),
    0x75CEC0: ("pppAngleLoop_Calc", "ppp operator pppAngleLoop, record +8, per particle calc"),
    0x75CF40: ("pppAngleLoop_Con", "ppp operator pppAngleLoop, record +28, per particle constructor at birth"),
    0x75CF70: ("pppAngleLoop_ReCon", "ppp operator pppAngleLoop, record +32, re-constructor on loop restart"),
    0x75CFE0: ("pppScale_Calc", "ppp operator pppScale, record +8, per particle calc"),
    0x75D050: ("pppScale_Con", "ppp operator pppScale, record +28 and +32, constructor at birth and on loop restart"),
    0x75D090: ("pppScaleLoop_Calc", "ppp operator pppScaleLoop, record +8, per particle calc"),
    0x75D130: ("pppScaleLoop_Con", "ppp operator pppScaleLoop, record +28, per particle constructor at birth"),
    0x75D160: ("pppScaleLoop_ReCon", "ppp operator pppScaleLoop, record +32, re-constructor on loop restart"),
    0x75D1F0: ("pppColor_Calc", "ppp operator pppColor, record +8, per particle calc"),
    0x75D240: ("pppColor_Con", "ppp operator pppColor, record +28 and +32, constructor at birth and on loop restart"),
    0x75D280: ("pppPObjPoint_Calc", "ppp operator pppPObjPoint, record +8, per particle calc"),
    0x75D340: ("pppVertexAttend_Calc", "ppp operator pppVertexAttend, record +8, per particle calc"),
    0x75D450: ("pppEiWindFun_Calc", "ppp operator pppEiWindFun, record +8, per particle calc"),
    0x75D590: ("pppEiWindFun_Con", "ppp operator pppEiWindFun, record +28, per particle constructor at birth"),
    0x75D5D0: ("pppNeiLightEikyo_Calc", "ppp operator pppNeiLightEikyo, record +8, per particle calc"),
    0x75D6C0: ("pppNeiLightEikyo_Con", "ppp operator pppNeiLightEikyo, record +28 and +32, constructor at birth and on loop restart"),
    0x75D700: ("pppNeiPointLight_Calc", "ppp operator pppNeiPointLight, record +8, per particle calc"),
    0x75D7C0: ("pppNeiPointLight_Con", "ppp operator pppNeiPointLight, record +28 and +32, constructor at birth and on loop restart"),
    0x75D7F0: ("pppFpPointLight_Calc", "ppp operator pppFpPointLight, record +8, per particle calc"),
    0x75D930: ("pppFpPointLightModel_Calc", "ppp operator pppFpPointLightModel, record +8, per particle calc"),
    0x75DB30: ("pppFpPointLightVsf_Calc", "ppp operator pppFpPointLightVsf, record +8, per particle calc"),
    0x75DD70: ("pppFpPointLightModelScl_Calc", "ppp operator pppFpPointLightModelScl, record +8, per particle calc"),
    0x75DF90: ("pppFpPointLightVsfScl_Calc", "ppp operator pppFpPointLightVsfScl, record +8, per particle calc"),
    0x75E310: ("pppKeMvYpEff_Calc", "ppp operator pppKeMvYpEff, record +8, per particle calc"),
    0x75E430: ("pppKeDrct_Calc", "ppp operator pppKeDrct, record +8, per particle calc"),
    0x75E470: ("pppKeDrct_Con", "ppp operator pppKeDrct, record +28, per particle constructor at birth"),

    # ppp helpers inside one operator's run, attributed by their callers or by their neighbours
    0x72DED0: ("pppKeMdlDtt__f72DED0", "helper of ppp operator pppKeMdlDtt, attributed by its callers or by sitting inside the operator's run"),
    0x72E6D0: ("pppKeMdlDtt__f72E6D0", "helper of ppp operator pppKeMdlDtt, attributed by its callers or by sitting inside the operator's run"),
    0x7326E0: ("pppSRandFV__f7326E0", "helper of ppp operator pppSRandFV, attributed by its callers or by sitting inside the operator's run"),
    0x732970: ("pppSRandUpFV__f732970", "helper of ppp operator pppSRandUpFV, attributed by its callers or by sitting inside the operator's run"),
    0x732C00: ("pppSRandDownFV__f732C00", "helper of ppp operator pppSRandDownFV, attributed by its callers or by sitting inside the operator's run"),
    0x732F30: ("pppSRandCV__f732F30", "helper of ppp operator pppSRandCV, attributed by its callers or by sitting inside the operator's run"),
    0x733230: ("pppSRandUpCV__f733230", "helper of ppp operator pppSRandUpCV, attributed by its callers or by sitting inside the operator's run"),
    0x733530: ("pppSRandDownCV__f733530", "helper of ppp operator pppSRandDownCV, attributed by its callers or by sitting inside the operator's run"),
    0x733820: ("pppSRandHCV__f733820", "helper of ppp operator pppSRandHCV, attributed by its callers or by sitting inside the operator's run"),
    0x733B30: ("pppSRandUpHCV__f733B30", "helper of ppp operator pppSRandUpHCV, attributed by its callers or by sitting inside the operator's run"),
    0x733E40: ("pppSRandDownHCV__f733E40", "helper of ppp operator pppSRandDownHCV, attributed by its callers or by sitting inside the operator's run"),
    0x7372D0: ("pppNeiChrPointLight__f7372D0", "helper of ppp operator pppNeiChrPointLight, attributed by its callers or by sitting inside the operator's run"),
    0x737700: ("pppDrawMdlInf__f737700", "helper of ppp operator pppDrawMdlInf, attributed by its callers or by sitting inside the operator's run"),
    0x73B5C0: ("pppDrawMdlSea__f73B5C0", "helper of ppp operator pppDrawMdlSea, attributed by its callers or by sitting inside the operator's run"),
    0x73C8E0: ("pppDrawMdlInf__f73C8E0", "helper of ppp operator pppDrawMdlInf, attributed by its callers or by sitting inside the operator's run"),
    0x73CE20: ("pppDrawMdlInf__f73CE20", "helper of ppp operator pppDrawMdlInf, attributed by its callers or by sitting inside the operator's run"),
    0x73CE30: ("pppDrawMdlInf__f73CE30", "helper of ppp operator pppDrawMdlInf, attributed by its callers or by sitting inside the operator's run"),
    0x73CE80: ("pppDrawMdlInf__f73CE80", "helper of ppp operator pppDrawMdlInf, attributed by its callers or by sitting inside the operator's run"),
    0x73CEB0: ("pppDrawMdlInf__f73CEB0", "helper of ppp operator pppDrawMdlInf, attributed by its callers or by sitting inside the operator's run"),
    0x73D4D0: ("pppDrawMdlPSim__f73D4D0", "helper of ppp operator pppDrawMdlPSim, attributed by its callers or by sitting inside the operator's run"),
    0x73D7C0: ("pppDrawMdlPSim__f73D7C0", "helper of ppp operator pppDrawMdlPSim, attributed by its callers or by sitting inside the operator's run"),
    0x73DFE0: ("pppDrawMdlCamera__f73DFE0", "helper of ppp operator pppDrawMdlCamera, attributed by its callers or by sitting inside the operator's run"),
    0x73E140: ("pppDrawMdlCamera__f73E140", "helper of ppp operator pppDrawMdlCamera, attributed by its callers or by sitting inside the operator's run"),
    0x7400F0: ("pppDrawMdlCameraLoop__f7400F0", "helper of ppp operator pppDrawMdlCameraLoop, attributed by its callers or by sitting inside the operator's run"),
    0x7401D0: ("pppDrawMdlRev__f7401D0", "helper of ppp operator pppDrawMdlRev, attributed by its callers or by sitting inside the operator's run"),
    0x740960: ("pppDrawMdlRev__f740960", "helper of ppp operator pppDrawMdlRev, attributed by its callers or by sitting inside the operator's run"),
    0x740A70: ("pppDrawShape__f740A70", "helper of ppp operator pppDrawShape, attributed by its callers or by sitting inside the operator's run"),
    0x740F60: ("pppDrawShapeX__f740F60", "helper of ppp operator pppDrawShapeX, attributed by its callers or by sitting inside the operator's run"),
    0x741470: ("pppDrawShapeRev__f741470", "helper of ppp operator pppDrawShapeRev, attributed by its callers or by sitting inside the operator's run"),
    0x7419E0: ("pppDrawShapeRev__f7419E0", "helper of ppp operator pppDrawShapeRev, attributed by its callers or by sitting inside the operator's run"),
    0x741B90: ("pppDrawShapeField__f741B90", "helper of ppp operator pppDrawShapeField, attributed by its callers or by sitting inside the operator's run"),
    0x742750: ("pppDrawShapeField__f742750", "helper of ppp operator pppDrawShapeField, attributed by its callers or by sitting inside the operator's run"),
    0x742980: ("pppDrawShapeField__f742980", "helper of ppp operator pppDrawShapeField, attributed by its callers or by sitting inside the operator's run"),
    0x742F20: ("pppDrawShapeField__f742F20", "helper of ppp operator pppDrawShapeField, attributed by its callers or by sitting inside the operator's run"),
    0x743030: ("pppDrawShapeField__f743030", "helper of ppp operator pppDrawShapeField, attributed by its callers or by sitting inside the operator's run"),
    0x743280: ("pppDrawShapeFieldRev__f743280", "helper of ppp operator pppDrawShapeFieldRev, attributed by its callers or by sitting inside the operator's run"),
    0x743B60: ("pppDrawShapeFieldRev__f743B60", "helper of ppp operator pppDrawShapeFieldRev, attributed by its callers or by sitting inside the operator's run"),
    0x743DA0: ("pppDrawShapeFieldRev__f743DA0", "helper of ppp operator pppDrawShapeFieldRev, attributed by its callers or by sitting inside the operator's run"),
    0x744260: ("pppDrawShapeFieldRev__f744260", "helper of ppp operator pppDrawShapeFieldRev, attributed by its callers or by sitting inside the operator's run"),
    0x7444A0: ("pppDrawShapeFieldSpd__f7444A0", "helper of ppp operator pppDrawShapeFieldSpd, attributed by its callers or by sitting inside the operator's run"),
    0x744D80: ("pppDrawShapeFieldSpd__f744D80", "helper of ppp operator pppDrawShapeFieldSpd, attributed by its callers or by sitting inside the operator's run"),
    0x744F60: ("pppDrawShapeFieldSpd__f744F60", "helper of ppp operator pppDrawShapeFieldSpd, attributed by its callers or by sitting inside the operator's run"),
    0x7454A0: ("pppDrawShapeFieldSpd__f7454A0", "helper of ppp operator pppDrawShapeFieldSpd, attributed by its callers or by sitting inside the operator's run"),
    0x745610: ("pppDrawShapeFieldGlobal__f745610", "helper of ppp operator pppDrawShapeFieldGlobal, attributed by its callers or by sitting inside the operator's run"),
    0x745740: ("pppDrawShapeFieldGlobal__f745740", "helper of ppp operator pppDrawShapeFieldGlobal, attributed by its callers or by sitting inside the operator's run"),
    0x745F70: ("pppDrawShapeFieldGlobal__f745F70", "helper of ppp operator pppDrawShapeFieldGlobal, attributed by its callers or by sitting inside the operator's run"),
    0x7460B0: ("pppDrawShapeFieldGlobal__f7460B0", "helper of ppp operator pppDrawShapeFieldGlobal, attributed by its callers or by sitting inside the operator's run"),
    0x746540: ("pppDrawShapeFieldGlobal__f746540", "helper of ppp operator pppDrawShapeFieldGlobal, attributed by its callers or by sitting inside the operator's run"),
    0x7467B0: ("pppDrawShapeCamera__f7467B0", "helper of ppp operator pppDrawShapeCamera, attributed by its callers or by sitting inside the operator's run"),
    0x747090: ("pppDrawShapeCamera__f747090", "helper of ppp operator pppDrawShapeCamera, attributed by its callers or by sitting inside the operator's run"),
    0x7472B0: ("pppDrawShapeCamera__f7472B0", "helper of ppp operator pppDrawShapeCamera, attributed by its callers or by sitting inside the operator's run"),
    0x747760: ("pppDrawShapeCamera__f747760", "helper of ppp operator pppDrawShapeCamera, attributed by its callers or by sitting inside the operator's run"),
    0x7479D0: ("pppDrawShapeCameraDisPos__f7479D0", "helper of ppp operator pppDrawShapeCameraDisPos, attributed by its callers or by sitting inside the operator's run"),
    0x7482E0: ("pppDrawShapeCameraDisPos__f7482E0", "helper of ppp operator pppDrawShapeCameraDisPos, attributed by its callers or by sitting inside the operator's run"),
    0x748500: ("pppDrawShapeCameraDisPos__f748500", "helper of ppp operator pppDrawShapeCameraDisPos, attributed by its callers or by sitting inside the operator's run"),
    0x7489B0: ("pppDrawShapeCameraDisPos__f7489B0", "helper of ppp operator pppDrawShapeCameraDisPos, attributed by its callers or by sitting inside the operator's run"),
    0x748B00: ("pppKeMdlBmp__f748B00", "helper of ppp operator pppKeMdlBmp, attributed by its callers or by sitting inside the operator's run"),
    0x749050: ("pppKeMdlBmp__f749050", "helper of ppp operator pppKeMdlBmp, attributed by its callers or by sitting inside the operator's run"),
    0x74A760: ("pppNeiDrawMdlPointLight__f74A760", "helper of ppp operator pppNeiDrawMdlPointLight, attributed by its callers or by sitting inside the operator's run"),
    0x74AD80: ("pppNeiDrawMdlSemiPointLight__f74AD80", "helper of ppp operator pppNeiDrawMdlSemiPointLight, attributed by its callers or by sitting inside the operator's run"),
    0x74B540: ("pppNeiDrawMdlTsPointLight__f74B540", "helper of ppp operator pppNeiDrawMdlTsPointLight, attributed by its callers or by sitting inside the operator's run"),
    0x74B9C0: ("pppNeiDrawShapePointLight__f74B9C0", "helper of ppp operator pppNeiDrawShapePointLight, attributed by its callers or by sitting inside the operator's run"),
    0x74BF10: ("pppKeShpTail__f74BF10", "helper of ppp operator pppKeShpTail, attributed by its callers or by sitting inside the operator's run"),
    0x74BFF0: ("pppKeShpTail__f74BFF0", "helper of ppp operator pppKeShpTail, attributed by its callers or by sitting inside the operator's run"),
    0x74C280: ("pppKeShpTail__f74C280", "helper of ppp operator pppKeShpTail, attributed by its callers or by sitting inside the operator's run"),
    0x74C3B0: ("pppKeShpTail__f74C3B0", "helper of ppp operator pppKeShpTail, attributed by its callers or by sitting inside the operator's run"),
    0x74CEF0: ("pppKeShpTailX__f74CEF0", "helper of ppp operator pppKeShpTailX, attributed by its callers or by sitting inside the operator's run"),
    0x74CFF0: ("pppKeShpTailX__f74CFF0", "helper of ppp operator pppKeShpTailX, attributed by its callers or by sitting inside the operator's run"),
    0x74D280: ("pppKeShpTailX__f74D280", "helper of ppp operator pppKeShpTailX, attributed by its callers or by sitting inside the operator's run"),
    0x74D3B0: ("pppKeShpTailX__f74D3B0", "helper of ppp operator pppKeShpTailX, attributed by its callers or by sitting inside the operator's run"),
    0x74DEF0: ("pppKeShpTail2__f74DEF0", "helper of ppp operator pppKeShpTail2, attributed by its callers or by sitting inside the operator's run"),
    0x74DFF0: ("pppKeShpTail2__f74DFF0", "helper of ppp operator pppKeShpTail2, attributed by its callers or by sitting inside the operator's run"),
    0x74E280: ("pppKeShpTail2__f74E280", "helper of ppp operator pppKeShpTail2, attributed by its callers or by sitting inside the operator's run"),
    0x74E3B0: ("pppKeShpTail2__f74E3B0", "helper of ppp operator pppKeShpTail2, attributed by its callers or by sitting inside the operator's run"),
    0x74EF50: ("pppKeShpTail2X__f74EF50", "helper of ppp operator pppKeShpTail2X, attributed by its callers or by sitting inside the operator's run"),
    0x74F030: ("pppKeShpTail2X__f74F030", "helper of ppp operator pppKeShpTail2X, attributed by its callers or by sitting inside the operator's run"),
    0x74F2C0: ("pppKeShpTail2X__f74F2C0", "helper of ppp operator pppKeShpTail2X, attributed by its callers or by sitting inside the operator's run"),
    0x74F3F0: ("pppKeShpTail2X__f74F3F0", "helper of ppp operator pppKeShpTail2X, attributed by its callers or by sitting inside the operator's run"),
    0x750130: ("pppKeShpTail3__f750130", "helper of ppp operator pppKeShpTail3, attributed by its callers or by sitting inside the operator's run"),
    0x7501D0: ("pppKeShpTail3__f7501D0", "helper of ppp operator pppKeShpTail3, attributed by its callers or by sitting inside the operator's run"),
    0x751680: ("pppKeShpTail3__f751680", "helper of ppp operator pppKeShpTail3, attributed by its callers or by sitting inside the operator's run"),
    0x751740: ("pppKeShpTail3__f751740", "helper of ppp operator pppKeShpTail3, attributed by its callers or by sitting inside the operator's run"),
    0x751930: ("pppKeShpTail3X__f751930", "helper of ppp operator pppKeShpTail3X, attributed by its callers or by sitting inside the operator's run"),
    0x7519D0: ("pppKeShpTail3X__f7519D0", "helper of ppp operator pppKeShpTail3X, attributed by its callers or by sitting inside the operator's run"),
    0x751AB0: ("pppKeShpTail3X__f751AB0", "helper of ppp operator pppKeShpTail3X, attributed by its callers or by sitting inside the operator's run"),
    0x753080: ("pppKeShpTail3X__f753080", "helper of ppp operator pppKeShpTail3X, attributed by its callers or by sitting inside the operator's run"),
    0x753230: ("pppKeShpTailPht__f753230", "helper of ppp operator pppKeShpTailPht, attributed by its callers or by sitting inside the operator's run"),
    0x753310: ("pppKeShpTailPht__f753310", "helper of ppp operator pppKeShpTailPht, attributed by its callers or by sitting inside the operator's run"),
    0x7535A0: ("pppKeShpTailPht__f7535A0", "helper of ppp operator pppKeShpTailPht, attributed by its callers or by sitting inside the operator's run"),
    0x753670: ("pppKeShpTailPht__f753670", "helper of ppp operator pppKeShpTailPht, attributed by its callers or by sitting inside the operator's run"),
    0x753DB0: ("pppKeShpTailPht__f753DB0", "helper of ppp operator pppKeShpTailPht, attributed by its callers or by sitting inside the operator's run"),
    0x755430: ("pppDrawRain__f755430", "helper of ppp operator pppDrawRain, attributed by its callers or by sitting inside the operator's run"),
    0x755A00: ("pppDrawRain__f755A00", "helper of ppp operator pppDrawRain, attributed by its callers or by sitting inside the operator's run"),
    0x756320: ("pppDrawRain__f756320", "helper of ppp operator pppDrawRain, attributed by its callers or by sitting inside the operator's run"),
    0x756740: ("pppDrawFilter__f756740", "helper of ppp operator pppDrawFilter, attributed by its callers or by sitting inside the operator's run"),
    0x756D20: ("pppDrawFilter__f756D20", "helper of ppp operator pppDrawFilter, attributed by its callers or by sitting inside the operator's run"),
    0x757770: ("pppVertexAp__f757770", "helper of ppp operator pppVertexAp, attributed by its callers or by sitting inside the operator's run"),
    0x757B50: ("pppVertexApDisPos__f757B50", "helper of ppp operator pppVertexApDisPos, attributed by its callers or by sitting inside the operator's run"),
    0x757ED0: ("pppVertexApLc__f757ED0", "helper of ppp operator pppVertexApLc, attributed by its callers or by sitting inside the operator's run"),
    0x758270: ("pppVertexApAt__f758270", "helper of ppp operator pppVertexApAt, attributed by its callers or by sitting inside the operator's run"),
    0x759400: ("pppKeBornPtCmpl__f759400", "helper of ppp operator pppKeBornPtCmpl, attributed by its callers or by sitting inside the operator's run"),
    0x75C660: ("pppKeOfsPt__f75C660", "helper of ppp operator pppKeOfsPt, attributed by its callers or by sitting inside the operator's run"),
    0x75E1F0: ("pppKeMvYpEff__f75E1F0", "helper of ppp operator pppKeMvYpEff, attributed by its callers or by sitting inside the operator's run"),

    # the rest of 0x720000..0x760000, positional names that only record the module
    0x7269A0: ("VfxPrim__f7269A0", "Virtuos prim build job helper"),
    0x7282D0: ("VfxPrim__f7282D0", "Virtuos prim build job helper"),
    0x7287B0: ("VfxPrim__f7287B0", "Virtuos prim build job helper"),
    0x728A60: ("MapFx__f728A60", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x728AC0: ("MapFx__f728AC0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x728DD0: ("MapFx__f728DD0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x728E40: ("MapFx__f728E40", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x728F10: ("MapFx__f728F10", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729050: ("MapFx__f729050", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729060: ("MapFx__f729060", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7291C0: ("MapFx__f7291C0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729280: ("MapFx__f729280", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729290: ("MapFx__f729290", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7292D0: ("MapFx__f7292D0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7292F0: ("MapFx__f7292F0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7293D0: ("MapFx__f7293D0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7293E0: ("MapFx__f7293E0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7293F0: ("MapFx__f7293F0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729400: ("MapFx__f729400", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729440: ("MapFx__f729440", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729480: ("MapFx__f729480", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7294F0: ("MapFx__f7294F0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729530: ("MapFx__f729530", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7295F0: ("MapFx__f7295F0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729660: ("MapFx__f729660", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7296A0: ("MapFx__f7296A0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729740: ("MapFx__f729740", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7297E0: ("MapFx__f7297E0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729810: ("MapFx__f729810", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729850: ("MapFx__f729850", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x7299E0: ("MapFx__f7299E0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729A10: ("MapFx__f729A10", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729F70: ("MapFx__f729F70", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x729F90: ("MapFx__f729F90", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A120: ("MapFx__f72A120", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A180: ("MapFx__f72A180", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A260: ("MapFx__f72A260", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A2E0: ("MapFx__f72A2E0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A530: ("MapFx__f72A530", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A590: ("MapFx__f72A590", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A5E0: ("MapFx__f72A5E0", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A630: ("MapFx__f72A630", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A690: ("MapFx__f72A690", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72A760: ("MapFx__f72A760", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72AB20: ("MapFx__f72AB20", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72AC20: ("MapFx__f72AC20", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72AC30: ("MapFx__f72AC30", "field placed particle (FP) glue, group API for ATEL and rcBg"),
    0x72C190: ("PppCore__f72C190", "ppp core, heap, matrix builders and boot glue"),
    0x72C1B0: ("PppCore__f72C1B0", "ppp core, heap, matrix builders and boot glue"),
    0x72C590: ("PppCore__f72C590", "ppp core, heap, matrix builders and boot glue"),
    0x72C5F0: ("PppCore__f72C5F0", "ppp core, heap, matrix builders and boot glue"),
    0x72C650: ("PppCore__f72C650", "ppp core, heap, matrix builders and boot glue"),
    0x72C6A0: ("PppCore__f72C6A0", "ppp core, heap, matrix builders and boot glue"),
    0x75E670: ("Oef2Util__f75E670", "helper called from sub_7ED0C0 in the Oef2 module"),
    0x75E6D0: ("Oef2Util__f75E6D0", "helper called from sub_7ED0C0 in the Oef2 module"),
    0x75E7D0: ("Vu0__f75E7D0", "PS2 VU0 macro emulation"),
    0x75E8F0: ("Vu0__f75E8F0", "PS2 VU0 macro emulation"),
    0x75ECA0: ("PppMath__f75ECA0", "math helper shared by ppp operators"),
    0x75EDE0: ("PppMath__f75EDE0", "math helper shared by ppp operators"),
    0x75EED0: ("PppMath__f75EED0", "math helper shared by ppp operators"),
    0x75F920: ("PppMath__f75F920", "math helper shared by ppp operators"),
    0x75FB50: ("VfxPm__f75FB50", "PS2 PMCOM remote particle tool side, callers in 0x760000.."),
    0x75FBF0: ("PppMath__f75FBF0", "math helper shared by ppp operators"),
    0x75FCD0: ("PppMath__f75FCD0", "math helper shared by ppp operators"),
    0x75FDA0: ("PppMath__f75FDA0", "math helper shared by ppp operators"),
    0x75FDE0: ("VfxPm__f75FDE0", "PS2 PMCOM remote particle tool side, callers in 0x760000.."),
    0x75FE70: ("VfxPm__f75FE70", "PS2 PMCOM remote particle tool side, callers in 0x760000.."),
    0x75FEE0: ("VfxPm__f75FEE0", "PS2 PMCOM remote particle tool side, callers in 0x760000.."),
    0x75FF00: ("VfxPm__f75FF00", "PS2 PMCOM remote particle tool side, callers in 0x760000.."),

    # Oef2 opcode handlers, g_ffxOef2SubCommandTable 0xC48EC8. Handlers take the 1024 byte dispatch
    # context and return the next IP. The op word is u16: low byte opcode, bit 8 selects the DLL table at
    # header+0x7C instead, bit 11 takes an operand from an object variable, bits 12..15 are a mode
    0x7F7E30: ("FFX_Oef2_Op132", "Oef2 opcode 132 handler, g_ffxOef2SubCommandTable[132]"),
    0x7FD720: ("FFX_Oef2_Op001_KillObject", "g_ffxOef2SubCommandTable[1]. clears all four thread IPs, yields, restarts the list walk, releases the link record through sub_7FE6C0, which reaches FFX_Btl_EffectAdvanceState"),
    0x8017D0: ("FFX_Oef2_Op103", "Oef2 opcode 103 handler, g_ffxOef2SubCommandTable[103]"),
    0x801A50: ("FFX_Oef2_Op104", "Oef2 opcode 104 handler, g_ffxOef2SubCommandTable[104]"),
    0x801AB0: ("FFX_Oef2_Op138", "Oef2 opcode 138 handler, g_ffxOef2SubCommandTable[138]"),
    0x801D00: ("FFX_Oef2_Op255", "Oef2 opcode 255 handler, g_ffxOef2SubCommandTable[255]"),
    0x803940: ("FFX_Oef2_Op170", "Oef2 opcode 170 handler, g_ffxOef2SubCommandTable[170]"),
    0x803C00: ("FFX_Oef2_Op137", "Oef2 opcode 137 handler, g_ffxOef2SubCommandTable[137]"),
    0x8044E0: ("FFX_Oef2_Op063_ChrMotion", "g_ffxOef2SubCommandTable[63]. CHR motion key, hokan and frame count on g_ffxEffectChrSlots[obj+0x18], mode nibble"),
    0x8047B0: ("maybe_FFX_Oef2_Op065_ChrSpeed", "g_ffxOef2SubCommandTable[65]. mode 0 FFX_Ch_SetSpeedMulA, modes 1 and 2 sub_787950"),
    0x804840: ("FFX_Oef2_Op026", "Oef2 opcode 26 handler, g_ffxOef2SubCommandTable[26]"),
    0x804B70: ("FFX_Oef2_Op211", "Oef2 opcode 211 handler, g_ffxOef2SubCommandTable[211]"),
    0x804C90: ("FFX_Oef2_Op152to153", "Oef2 opcodes 152 153, one handler"),
    0x807F90: ("FFX_Oef2_Op150", "Oef2 opcode 150 handler, g_ffxOef2SubCommandTable[150]"),
    0x808000: ("FFX_Oef2_Op040", "Oef2 opcode 40 handler, g_ffxOef2SubCommandTable[40]"),
    0x808380: ("FFX_Oef2_Op171", "Oef2 opcode 171 handler, g_ffxOef2SubCommandTable[171]"),
    0x8088C0: ("FFX_Oef2_Op041", "Oef2 opcode 41 handler, g_ffxOef2SubCommandTable[41]"),
    0x808CF0: ("FFX_Oef2_Op168", "Oef2 opcode 168 handler, g_ffxOef2SubCommandTable[168]"),
    0x809180: ("FFX_Oef2_Op064", "Oef2 opcode 64 handler, g_ffxOef2SubCommandTable[64]"),
    0x809620: ("FFX_Oef2_Op056", "Oef2 opcode 56 handler, g_ffxOef2SubCommandTable[56]"),
    0x809F90: ("FFX_Oef2_Op144", "Oef2 opcode 144 handler, g_ffxOef2SubCommandTable[144]"),
    0x80ACA0: ("FFX_Oef2_Op085", "Oef2 opcode 85 handler, g_ffxOef2SubCommandTable[85]"),
    0x80AD20: ("FFX_Oef2_Op038", "Oef2 opcode 38 handler, g_ffxOef2SubCommandTable[38]"),
    0x80ADC0: ("FFX_Oef2_Op042", "Oef2 opcode 42 handler, g_ffxOef2SubCommandTable[42]"),
    0x80AE30: ("FFX_Oef2_Op050", "Oef2 opcode 50 handler, g_ffxOef2SubCommandTable[50]"),
    0x80AF70: ("FFX_Oef2_Op093", "Oef2 opcode 93 handler, g_ffxOef2SubCommandTable[93]"),
    0x80B460: ("FFX_Oef2_Op131", "Oef2 opcode 131 handler, g_ffxOef2SubCommandTable[131]"),
    0x80B670: ("FFX_Oef2_Op220", "Oef2 opcode 220 handler, g_ffxOef2SubCommandTable[220]"),
    0x80B7D0: ("FFX_Oef2_Op222", "Oef2 opcode 222 handler, g_ffxOef2SubCommandTable[222]"),
    0x80C3F0: ("FFX_Oef2_Op000_End", "g_ffxOef2SubCommandTable[0]. ends the thread. Thread 0 also clears all four IPs and frees the object (sub_8014C0, sub_800D10), but waits 120 while its link record is still a pending type 4"),
    0x80C490: ("FFX_Oef2_Op127_AdvanceBattleEffect", "g_ffxOef2SubCommandTable[127]. once per link record (rec+29 latch) FFX_Btl_EffectAdvanceState(rec+0), then optional End"),
    0x80C500: ("FFX_Oef2_Op009_Wait", "g_ffxOef2SubCommandTable[9]. yield count = op word >> 9"),
    0x80C520: ("FFX_Oef2_Op176", "Oef2 opcode 176 handler, g_ffxOef2SubCommandTable[176]"),
    0x80C650: ("FFX_Oef2_Op029", "Oef2 opcode 29 handler, g_ffxOef2SubCommandTable[29]"),
    0x80C6A0: ("FFX_Oef2_Op169and173", "Oef2 opcodes 169 173, one handler"),
    0x80C7C0: ("FFX_Oef2_Op164", "Oef2 opcode 164 handler, g_ffxOef2SubCommandTable[164]"),
    0x80C940: ("FFX_Oef2_Op156", "Oef2 opcode 156 handler, g_ffxOef2SubCommandTable[156]"),
    0x80CA80: ("FFX_Oef2_Op175", "Oef2 opcode 175 handler, g_ffxOef2SubCommandTable[175]"),
    0x80F210: ("FFX_Oef2_Op078", "Oef2 opcode 78 handler, g_ffxOef2SubCommandTable[78]"),
    0x8139B0: ("FFX_Oef2_Op133", "Oef2 opcode 133 handler, g_ffxOef2SubCommandTable[133]"),
    0x814150: ("FFX_Oef2_Op043", "Oef2 opcode 43 handler, g_ffxOef2SubCommandTable[43]"),
    0x8143E0: ("FFX_Oef2_Op108", "Oef2 opcode 108 handler, g_ffxOef2SubCommandTable[108]"),
    0x814440: ("FFX_Oef2_Op109", "Oef2 opcode 109 handler, g_ffxOef2SubCommandTable[109]"),
    0x814520: ("FFX_Oef2_Op082", "Oef2 opcode 82 handler, g_ffxOef2SubCommandTable[82]"),
    0x814790: ("FFX_Oef2_Op213", "Oef2 opcode 213 handler, g_ffxOef2SubCommandTable[213]"),
    0x814800: ("FFX_Oef2_Op084", "Oef2 opcode 84 handler, g_ffxOef2SubCommandTable[84]"),
    0x815710: ("FFX_Oef2_Op058", "Oef2 opcode 58 handler, g_ffxOef2SubCommandTable[58]"),
    0x815760: ("FFX_Oef2_Op003_Call", "g_ffxOef2SubCommandTable[3]. saves IP+4 in the thread return slot obj+0x20+4*thread, then jumps by s16 at IP+2"),
    0x8157A0: ("FFX_Oef2_Op057_Camera", "g_ffxOef2SubCommandTable[57]. effect camera, FFX_Came_SetPos eye and target from object vectors with per magic id fix ups, mode nibble"),
    0x815E10: ("FFX_Oef2_Op086", "Oef2 opcode 86 handler, g_ffxOef2SubCommandTable[86]"),
    0x815E80: ("FFX_Oef2_Op061", "Oef2 opcode 61 handler, g_ffxOef2SubCommandTable[61]"),
    0x815F10: ("FFX_Oef2_Op053", "Oef2 opcode 53 handler, g_ffxOef2SubCommandTable[53]"),
    0x815F60: ("FFX_Oef2_Op201", "Oef2 opcode 201 handler, g_ffxOef2SubCommandTable[201]"),
    0x816030: ("FFX_Oef2_Op100", "Oef2 opcode 100 handler, g_ffxOef2SubCommandTable[100]"),
    0x8161F0: ("FFX_Oef2_Op151_ChrVisibility", "g_ffxOef2SubCommandTable[151]. CHR hide bits 1 and 2, battle unit show votes through sub_796660, mode nibble"),
    0x8165D0: ("FFX_Oef2_Op126", "Oef2 opcode 126 handler, g_ffxOef2SubCommandTable[126]"),
    0x816610: ("FFX_Oef2_Op136", "Oef2 opcode 136 handler, g_ffxOef2SubCommandTable[136]"),
    0x8167B0: ("FFX_Oef2_Op242", "Oef2 opcode 242 handler, g_ffxOef2SubCommandTable[242]"),
    0x816870: ("FFX_Oef2_Op031", "Oef2 opcode 31 handler, g_ffxOef2SubCommandTable[31]"),
    0x8168B0: ("FFX_Oef2_Op002_Jump", "g_ffxOef2SubCommandTable[2]. IP += s16 at IP+2"),
    0x8168C0: ("FFX_Oef2_Op161", "Oef2 opcode 161 handler, g_ffxOef2SubCommandTable[161]"),
    0x816E50: ("FFX_Oef2_Op162", "Oef2 opcode 162 handler, g_ffxOef2SubCommandTable[162]"),
    0x8170C0: ("FFX_Oef2_Op033", "Oef2 opcode 33 handler, g_ffxOef2SubCommandTable[33]"),
    0x8171A0: ("FFX_Oef2_Op083", "Oef2 opcode 83 handler, g_ffxOef2SubCommandTable[83]"),
    0x817250: ("FFX_Oef2_Op051", "Oef2 opcode 51 handler, g_ffxOef2SubCommandTable[51]"),
    0x817270: ("FFX_Oef2_Op087", "Oef2 opcode 87 handler, g_ffxOef2SubCommandTable[87]"),
    0x817280: ("FFX_Oef2_Op004_Return", "g_ffxOef2SubCommandTable[4]. IP = the thread return slot obj+0x20+4*thread"),
    0x8172B0: ("FFX_Oef2_Op092", "Oef2 opcode 92 handler, g_ffxOef2SubCommandTable[92]"),
    0x817300: ("FFX_Oef2_Op030", "Oef2 opcode 30 handler, g_ffxOef2SubCommandTable[30]"),
    0x817470: ("FFX_Oef2_Op079_ObjFlags", "g_ffxOef2SubCommandTable[79]. obj+0xB2 bits 0..2, bit 0 paired with FFX_Btl_SetExplodeActive"),
    0x817530: ("FFX_Oef2_Op125", "Oef2 opcode 125 handler, g_ffxOef2SubCommandTable[125]"),
    0x8176F0: ("FFX_Oef2_Op005_UnitFlagBits", "g_ffxOef2SubCommandTable[5]. set, clear, branch on or wait on bits of unit+4, mode in the op word top nibble"),
    0x8177F0: ("FFX_Oef2_Op047", "Oef2 opcode 47 handler, g_ffxOef2SubCommandTable[47]"),
    0x8178F0: ("FFX_Oef2_Op062", "Oef2 opcode 62 handler, g_ffxOef2SubCommandTable[62]"),
    0x8179B0: ("FFX_Oef2_Op046", "Oef2 opcode 46 handler, g_ffxOef2SubCommandTable[46]"),
    0x817AD0: ("FFX_Oef2_Op112_ChrI408", "g_ffxOef2SubCommandTable[112]. test, branch, wait on or set CHR i408 (FFX_Ch_GetI408, FFX_Ch_SetI408)"),
    0x817E60: ("FFX_Oef2_Op142", "Oef2 opcode 142 handler, g_ffxOef2SubCommandTable[142]"),
    0x817EE0: ("FFX_Oef2_Op099", "Oef2 opcode 99 handler, g_ffxOef2SubCommandTable[99]"),
    0x817F50: ("FFX_Oef2_Op114", "Oef2 opcode 114 handler, g_ffxOef2SubCommandTable[114]"),
    0x818110: ("FFX_Oef2_Op110", "Oef2 opcode 110 handler, g_ffxOef2SubCommandTable[110]"),
    0x818200: ("FFX_Oef2_Op202", "Oef2 opcode 202 handler, g_ffxOef2SubCommandTable[202]"),
    0x8182F0: ("FFX_Oef2_Op115", "Oef2 opcode 115 handler, g_ffxOef2SubCommandTable[115]"),
    0x818410: ("FFX_Oef2_Op010to015_SetVector", "opcodes 10..15, writes an object vector, field offset word_C49388[op-10], scales unk_C49398, then calls the object move function"),
    0x8187B0: ("FFX_Oef2_Op146", "Oef2 opcode 146 handler, g_ffxOef2SubCommandTable[146]"),
    0x818A30: ("FFX_Oef2_Op224", "Oef2 opcode 224 handler, g_ffxOef2SubCommandTable[224]"),
    0x818DC0: ("FFX_Oef2_Op113", "Oef2 opcode 113 handler, g_ffxOef2SubCommandTable[113]"),
    0x818ED0: ("FFX_Oef2_Op095", "Oef2 opcode 95 handler, g_ffxOef2SubCommandTable[95]"),
    0x8190E0: ("FFX_Oef2_Op072_UnitMove", "g_ffxOef2SubCommandTable[72]. FFX_Btl_StartUnitMove, unit flag 1042, and FFX_Oef2_SetUnitOrChrPos from obj+0x90, mode nibble. Writes CHR position on the field"),
    0x819450: ("FFX_Oef2_Op140", "Oef2 opcode 140 handler, g_ffxOef2SubCommandTable[140]"),
    0x819940: ("FFX_Oef2_Op097", "Oef2 opcode 97 handler, g_ffxOef2SubCommandTable[97]"),
    0x819A20: ("FFX_Oef2_Op210", "Oef2 opcode 210 handler, g_ffxOef2SubCommandTable[210]"),
    0x819AF0: ("FFX_Oef2_Op129", "Oef2 opcode 129 handler, g_ffxOef2SubCommandTable[129]"),
    0x819C90: ("FFX_Oef2_Op054", "Oef2 opcode 54 handler, g_ffxOef2SubCommandTable[54]"),
    0x819FD0: ("FFX_Oef2_Op214to219", "Oef2 opcodes 214 215 216 217 218 219, one handler"),
    0x81A4A0: ("FFX_Oef2_Op018to023_SetVectorRandom", "opcodes 18..23, like 10..15 with each lane times a draw from g_ffxEffectRandState in -1..1"),
    0x81ABD0: ("FFX_Oef2_Op225", "Oef2 opcode 225 handler, g_ffxOef2SubCommandTable[225]"),
    0x81ABE0: ("FFX_Oef2_Op172", "Oef2 opcode 172 handler, g_ffxOef2SubCommandTable[172]"),
    0x81AFA0: ("FFX_Oef2_Op055", "Oef2 opcode 55 handler, g_ffxOef2SubCommandTable[55]"),
    0x81B4D0: ("FFX_Oef2_Op090", "Oef2 opcode 90 handler, g_ffxOef2SubCommandTable[90]"),
    0x81B560: ("FFX_Oef2_Op134", "Oef2 opcode 134 handler, g_ffxOef2SubCommandTable[134]"),
    0x81B8D0: ("FFX_Oef2_Op036to037", "Oef2 opcodes 36 37, one handler"),
    0x81B9B0: ("FFX_Oef2_Op074", "Oef2 opcode 74 handler, g_ffxOef2SubCommandTable[74]"),
    0x81BBC0: ("FFX_Oef2_Op143", "Oef2 opcode 143 handler, g_ffxOef2SubCommandTable[143]"),
    0x81BEF0: ("FFX_Oef2_Op077", "Oef2 opcode 77 handler, g_ffxOef2SubCommandTable[77]"),
    0x81C140: ("FFX_Oef2_Op035", "Oef2 opcode 35 handler, g_ffxOef2SubCommandTable[35]"),
    0x81C160: ("FFX_Oef2_Op107", "Oef2 opcode 107 handler, g_ffxOef2SubCommandTable[107]"),
    0x81C260: ("FFX_Oef2_Op081", "Oef2 opcode 81 handler, g_ffxOef2SubCommandTable[81]"),
    0x81C460: ("FFX_Oef2_Op060", "Oef2 opcode 60 handler, g_ffxOef2SubCommandTable[60]"),
    0x81C4F0: ("FFX_Oef2_Op111", "Oef2 opcode 111 handler, g_ffxOef2SubCommandTable[111]"),
    0x81CA60: ("FFX_Oef2_Op068", "Oef2 opcode 68 handler, g_ffxOef2SubCommandTable[68]"),

    # opp kind handlers, g_ffxOppKindTable 0xC48E78, indexed by obj+0xBB when it is not negative
    0x811120: ("FFX_Opp_Kind01", "g_ffxOppKindTable[1], opp_main calls it for objects whose obj+0xBB is 1"),
    0x8020A0: ("FFX_Opp_Kind02", "g_ffxOppKindTable[2], opp_main calls it for objects whose obj+0xBB is 2"),
    0x812FA0: ("FFX_Opp_Kind04", "g_ffxOppKindTable[4], opp_main calls it for objects whose obj+0xBB is 4"),
    0x813410: ("FFX_Opp_Kind05", "g_ffxOppKindTable[5], opp_main calls it for objects whose obj+0xBB is 5"),
    0x813020: ("FFX_Opp_Kind06", "g_ffxOppKindTable[6], opp_main calls it for objects whose obj+0xBB is 6"),
    0x80F680: ("FFX_Opp_Kind07", "g_ffxOppKindTable[7], opp_main calls it for objects whose obj+0xBB is 7"),
    0x8124B0: ("FFX_Opp_Kind09", "g_ffxOppKindTable[9], opp_main calls it for objects whose obj+0xBB is 9"),
    0x811530: ("FFX_Opp_Kind10", "g_ffxOppKindTable[10], opp_main calls it for objects whose obj+0xBB is 10"),
    0x813460: ("FFX_Opp_Kind11", "g_ffxOppKindTable[11], opp_main calls it for objects whose obj+0xBB is 11"),
    0x812B50: ("FFX_Opp_Kind12", "g_ffxOppKindTable[12], opp_main calls it for objects whose obj+0xBB is 12"),
    0x80DF10: ("FFX_Opp_Kind14", "g_ffxOppKindTable[14], opp_main calls it for objects whose obj+0xBB is 14"),
    0x80E030: ("FFX_Opp_Kind15", "g_ffxOppKindTable[15], opp_main calls it for objects whose obj+0xBB is 15"),
    0x802350: ("FFX_Opp_Kind16", "g_ffxOppKindTable[16], opp_main calls it for objects whose obj+0xBB is 16"),
    0x811BC0: ("FFX_Opp_Kind17", "g_ffxOppKindTable[17], opp_main calls it for objects whose obj+0xBB is 17"),
    0x80EB50: ("FFX_Opp_Kind18", "g_ffxOppKindTable[18], opp_main calls it for objects whose obj+0xBB is 18"),

    # object move functions, g_ffxOef2MoveFnTable 0xC492C8, indexed by obj+0xBA after the integrator
    0x815050: ("FFX_Oef2_Move00", "g_ffxOef2MoveFnTable[0], called with (ctx, ip) for objects whose obj+0xBA is 0. Slot 0 is never reached from the integrator, which skips 0"),
    0x814C90: ("FFX_Oef2_Move03", "g_ffxOef2MoveFnTable[3], called with (ctx, ip) for objects whose obj+0xBA is 3"),
    0x8155B0: ("FFX_Oef2_Move04", "g_ffxOef2MoveFnTable[4], called with (ctx, ip) for objects whose obj+0xBA is 4"),
    0x814F00: ("FFX_Oef2_Move05", "g_ffxOef2MoveFnTable[5], called with (ctx, ip) for objects whose obj+0xBA is 5"),
    0x815120: ("FFX_Oef2_Move06", "g_ffxOef2MoveFnTable[6], called with (ctx, ip) for objects whose obj+0xBA is 6"),
    0x814D20: ("FFX_Oef2_Move07", "g_ffxOef2MoveFnTable[7], called with (ctx, ip) for objects whose obj+0xBA is 7"),
    0x814E50: ("FFX_Oef2_Move08", "g_ffxOef2MoveFnTable[8], called with (ctx, ip) for objects whose obj+0xBA is 8"),
    0x814FA0: ("FFX_Oef2_Move09", "g_ffxOef2MoveFnTable[9], called with (ctx, ip) for objects whose obj+0xBA is 9"),
    0x815670: ("FFX_Oef2_Move10", "g_ffxOef2MoveFnTable[10], called with (ctx, ip) for objects whose obj+0xBA is 10"),
    0x815350: ("FFX_Oef2_Move11", "g_ffxOef2MoveFnTable[11], called with (ctx, ip) for objects whose obj+0xBA is 11"),
}

# ---------------------------------------------------------------------------------------------
# Functions whose current name is wrong. None found in this pass
# ---------------------------------------------------------------------------------------------

RENAMES = {
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xC48E78: ("g_ffxOppKindTable", "20 opp kind handlers called by opp_main (0x80C3C8) for obj+0xBB >= 0. Slots 0, 3 and 19 are empty, kind 3 is the ppp part and runs through Oef2 op 221 opu_part_run. Negative kinds use the header +0x78 table"),
    0xC492C8: ("g_ffxOef2MoveFnTable", "12 object move functions, FFX_Magic_Oef2Dispatch calls [obj+0xBA] at 0x80D998 for 1..11 after the integrator, and SetVector ops call it too. Slots 1 and 2 are 0"),
    0xC3A508: ("g_ffxPppOpTable", "145 x 40 byte ppp operator records, the exe's own set, used by FFX_MapFx_LoadParticleSet and FFX_MagicResident_HdrRelocateParticles. Record {name, group, calc, draw, init, prepass, postpass, con, recon, des}"),
    0xC3BBB0: ("g_ffxPppOpTableForDlls", "273 x 40 byte ppp operator records, reached only by magic DLLs through host data slot 921 (0xC65B4C)"),
    0xC3E7A8: ("g_ffxPppOpTablePm", "246 x 40 byte ppp operator records for the PS2 PMCOM remote particle tool, FFX_Vfx_PumpCommandRing pushes 0xC3E780"),
    0xC86080: ("maybe_g_ffxPppOpTableMenu", "32 x 40 byte ppp operator records, used by sub_A54760 (caller sub_8E27D0)"),
    0x12A4100: ("g_ffxEffectLinkRecords", "512 x 32 byte records tying an Oef2 object to its battle effect. +0 battle effect handle for FFX_Btl_EffectAdvanceState, +4 object, +8 target count, +16 target slots, +24 caster slot, +29 advanced latch. obj+0x14 indexes it"),
    0x12A8100: ("g_ffxEffectChrSlots", "32 CHR pointers, the slots Oef2 objects address by obj+0x18. Field and battle effect ops reach CHR state through it"),
    0x230FD38: ("g_ffxVfxSlotLoopOff", "1 skips the FFX_Vfx_StepAll slot loop. sub_761D60 sets it at boot and only a PMCOM part start command (FFX_Vfx_PumpCommandRing 0x761AEB) clears it, so retail only pumps the command ring"),
    0xC88A90: ("g_ffxResidentEffectUnitPtr", "points at g_ffxResidentEffectUnit, effect unit 0. The next five dwords point at its pass 0, 1, 2 lists, its opp list and its object array"),
    0x22DA070: ("g_ffxResidentEffectUnit", "effect unit 0, the exe's own, 0x100 byte header then 4 x 1024 byte u16 lists, maxObjs 512. Built at boot by sub_7FD760 through FFX_Effect_InitUnit"),
    0x22DB170: ("g_ffxResidentEffectObjects", "512 x 256 byte Oef2 objects of effect unit 0"),
    0x113FCD0: ("g_ffxEffectViewMatrix", "FFX_Came_GetViewMatrix copy taken by FFX_MagicResident_OvlEffectsPass, copied into every Oef2 dispatch context at +640"),
    0x113FD10: ("g_ffxEffectProjMatrix", "FFX_Came_GetProjMatrix copy, copied into every Oef2 dispatch context at +704"),
    0x12F4118: ("g_ffxOef2CurrentIp", "the IP of the Oef2 opcode being dispatched, written before every handler call"),
    0xC3A4CC: ("g_ffxPppCurrentHeap", "pointer to the ppp heap FFX_PppHeap_Alloc and Free work on, swapped per caller (FFX_Vfx_StepAll, MapFx)"),
}


PLACEHOLDER = re.compile(
    r"^(sub|nullsub|loc|j_sub|unk|dword|word|byte|qword|off|flt|dbl|stru|funcs)_[0-9A-Fa-f]+$"
    r"|^[A-Za-z][A-Za-z0-9]*__f[0-9A-Fa-f]{6}$"
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
