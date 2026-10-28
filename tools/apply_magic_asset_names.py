"""
Apply the magic texture names from reversing/MAGIC_ASSETS.md to the FFX.exe IDB.

The magic textures load in three steps, all keyed by the GS TEX0 the PS2 data already carries:

    step thread   MagicFile__load -> the DLL (RegistTask on the worker, ASYNC_LOAD_REQUESTS H4)
                  FFX_MainStep_PollMagicPrxLoad -> MagicFile__requestTexListOnce
                  -> FFX_Magic_EnqueueTexList builds magic/<name>/tex[_xx]/TexList.txt
    worker        FFX_TexList_LoadAndEnqueueA parses it, one LoadCluster per row
                  -> FFX_Tex_OnTexListRowLoaded fills the gfx slot tables (H3)
    draw          FFX_MagicTex_ResolveByTex0 maps (TBP, PSM, size) back to a loaded path

Several rows reuse a name already proposed, not applied, in ASYNC_LOAD_REQUESTS.md section 7 or
ASSET_PATHS.md section 9, so one address keeps one proposed name.

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

    python apply_magic_asset_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the load mode, dword_CCB464
    0x6435E0: ("FFX_Magic_SetPrxLoadMode", "writes g_ffxMagicPrxLoadMode at 0x6435FA, refused while FFX_LoadDll_IsBusy or gfx+36 == 1. From MagicFile__selectLoadMode, MagicFilePreload__preload and MagicFile__unload (mode 1)"),

    # MagicFile, the one loaded magic DLL
    0x9DA280: ("MagicFile__requestTexListOnce", "first call after the DLL is in: FFX_Magic_RequestTexList(g_ffxMagicFileId), g_ffxMagicTexListRequested = 1. Returns 0 while there is no module. From FFX_MainStep_PollMagicPrxLoad and FFX_Btl_BeginMagicEffect"),
    0x9DA2C0: ("MagicFile__wasReused", "returns g_ffxMagicFileReused. The poll starts the effect at once when set, no DLL wait and no TexList wait"),
    0x9DA2D0: ("MagicFile__retireCurrent", "clears the gfx+64252 name, moves id and hinst to the prev pair (g_ffxMagicFilePrevId), current id and hinst -1. From FFX_Btl_StartMagicPrx and MagicFile__stop"),
    0x9DA390: ("MagicFile__getHinst", "returns g_ffxMagicFileHinst. -1 means no magic DLL, the texture resolver and FFX_Magic_DrawSpriteTex0 test it"),
    0x9DA3D0: ("MagicFile__getPrevMagicId", "returns g_ffxMagicFilePrevId. While it is not -1 the resolver only accepts magic slots whose path holds the current id"),

    # TexList request and load, kind 2 is magic
    0x6420B0: ("FFX_Magic_RequestTexList", "cdecl thunk, FFX_Magic_EnqueueTexList on g_ffxGfxCtx. From MagicFile__requestTexListOnce and MagicFile__load (sync mode only)"),
    0x664D10: ("FFX_AsyncLoad_EnqueueTexListTaskA", "copies the path, AsyncLoadManager__enqueue(FFX_AsyncLoad__texListJobA). Only caller FFX_Magic_EnqueueTexList with kind 2"),
    0x657880: ("FFX_AsyncLoad__texListJobA", "worker body, FFX_TexList_LoadAndEnqueueA(gfx, kind, path, 1)"),
    0x664550: ("FFX_TexList_LoadAndEnqueueA", "WORKER. No TexList: gfx+36 = 1, return 5. Per row tries <dir>D3D11/<row>, then tex/D3D11/<row> when the dir ends _xx, a row with neither breaks and returns 5 leaving gfx+36 at 0. Grows the slot arrays (sub_667E80), one LoadCluster per row with FFX_Tex_OnTexListRowLoaded, then gfx+36 = 1"),
    0x666880: ("FFX_Tex_OnTexListRowLoaded", "WORKER LoadCluster callback for every TexList kind: linkImports, then gfx+96[i] = 1, +60[i] name, +84[i] path, FFX_Tex_RegisterSlot. The loaded flag is set without checking that the cluster loaded"),
    0x656AE0: ("FFX_Tex_RegisterSlot", "sscanf %d_%d_%d_%d_%d_%d of the row name into the 20 byte gfx+72 record: +0 kind, +4 TBP, +8 PSM, +14 w, +16 h. A trxtex name gets TBP -1"),
    0x6522B0: ("FFX_Magic_AreTexturesLoaded", "STEP: 1 only when gfx+36 is set and every slot with +84 set has +96 == 1, then clears gfx+36. The load mode 2 gate in FFX_MainStep_PollMagicPrxLoad"),
    0x6420D0: ("j_FFX_Magic_AreTexturesLoaded", "thunk, called at 0x8222CD and 0x822336"),
    0x65EFD0: ("FFX_Magic_HasLocalizedTextures", "true for 7, 8, 32, 40, 46, 48, 50, 405. Those ask for tex_<code2>/TexList.txt outside jp, kr and ch. magic_0048 ships no tex_xx list"),

    # release
    0x6420F0: ("FFX_Magic_ReleaseTexList", "cdecl thunk, FFX_Gfx_ReleaseMagicTexList on g_ffxGfxCtx, from MagicFile__unload"),
    0x670F00: ("FFX_Gfx_ReleaseMagicTexList", "drops the id from the gfx+40/+48 requested list, then FFX_TexList_Release(magic/<name>/tex/TexList.txt, id)"),
    0x671300: ("FFX_TexList_Release", "frees the slots a TexList loaded through the cluster cache. For magic, ids 630..637 keep the 00001..00003 1x1 textures"),

    # lookup at draw time
    0x6604D0: ("FFX_MagicTex_ResolveByTex0", "(tbp, psm, f3, f4, w, h, out path). One entry cache gfx+100..+112, hardcoded override paths (return 1, then a cluster cache find), then a slot scan: skips kind 2 outside a magic overlay call, kind 1 inside one, kind 0 when dword_CCB9D0. Matches TBP 0x6612CA and PSM 0x6612D4, picks duplicate TBP variants by g_ffxMagicTexVariant, returns 2"),
    0x661C80: ("FFX_MagicTex_ResolveByName", "the by name sibling of FFX_MagicTex_ResolveByTex0, same slot tables"),
    0x645450: ("FFX_Host_ResolveTexByTex0", "host slot 885. FFX_MagicTex_ResolveByTex0, a 1 is confirmed through the cluster cache find sub_69B4B0. Called by the ppp and opp draws and 19 DLLs (21 sites)"),
    0x645400: ("FFX_Host_ResolveTexByName", "host slot 840, FFX_MagicTex_ResolveByName. No shipped DLL calls the slot, exe draws do"),

    # the PS2 texture records in the DLL .data
    0x7E4830: ("FFX_Host_QueueTexRange", "host slot 222, 410 DLLs, 556 sites. Queues sub table 2 texture records and sub table 3 CLUT records of an Oef2 block into the 64 entry upload ring, and sets g_ffxMagicTexVariant for some ids"),
    0x7E4F20: ("FFX_Tex_QueueTexUpload", "texture record into g_ffxTexUploadQueue, or maybe_FFX_Tex_VramResidency outside TBP 0x3800..0x3BFF when byte_11A0066 is set"),
    0x7E4610: ("FFX_Tex_QueueClutUpload", "CLUT record, same ring and same residency branch as FFX_Tex_QueueTexUpload"),
    0x7E56D0: ("maybe_FFX_Tex_VramResidency", "the PS2 VRAM residency tracker behind the two queue calls. Not read in full"),
    0x62F820: ("maybe_FFX_Gs_LoadImage425_Stub", "host slot 425, return 0. Called by op_oef2_texture_load and other PS2 upload paths, no DLL calls the slot. The PS2 pixels never reach the GPU"),
    0x62F7A0: ("maybe_FFX_Gs_LoadImage423_Stub", "host slot 423, return 0, the twin of slot 425. No DLL calls the slot"),

    # host 812, the 2D sprite path
    0x8AB440: ("FFX_Path_InternString", "returns a stable copy from a 64 x 256 byte pool at byte_25CC5A0, reusing an equal string. From FFX_Magic_DrawSpriteTex0, FFX_SaMenu_Draw, FFX_Font_ResolvePageTexture"),
    0x8E5200: ("FFX_Sprite_AddTexPath", "adds a path to the 10 entry g_ffxSpriteTexPaths list if not present"),
    0x8E89D0: ("FFX_Sprite_ReleaseTexPaths", "releases every g_ffxSpriteTexPaths entry through sub_642360 and empties the list. From FFX_BtlOd_WakkaReels_StopCue (host 649)"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x906420: ("FFX_Magic_DrawSpriteTex0", "was FFX_Magic_BuildTexturePath. Host slot 812 is a 2D sprite draw: it clips the quad, decodes a 64 bit TEX0, builds magic_%04d/tex/GCM/<tbp>_<psm>_0_0_<w>_<h>.dds.phyre, interns it and calls FFX_Sprite2D_Draw. 10 DLLs, 140 sites. The ordinary magic textures come from the TexList, not from here"),
    0x9DAFE0: ("MagicFile__selectLoadMode", "was MagicFile__selectEffectBudget. It picks the PRX load mode, not a budget: 1 for 622..628, 348, 631, 635, 3 for 350, 2 otherwise, through FFX_Magic_SetPrxLoadMode. Mode 1 starts the effect before its textures are in, 2 and 3 wait for them"),
    0x641C60: ("FFX_Magic_GetPrxLoadMode", "was maybe_FFX_IsAsyncPrxLoad. Returns g_ffxMagicPrxLoadMode, 0 sync, 1 async no texture wait, 2 and 3 async with the texture wait. Callers test it as a bool except FFX_MainStep_PollMagicPrxLoad, which compares it with 1"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xCCB464: ("g_ffxMagicPrxLoadMode", "0 sync, 1 async and start before the textures, 2 and 3 async and wait for them. Uninitialized, so 0 at boot. Written only by FFX_Magic_SetPrxLoadMode"),
    0xCCB9D4: ("g_ffxMagicTexVariant", "picks between slots that share a TBP. Set by FFX_Host_QueueTexRange for some ids, cleared by MagicFile__stop, read by FFX_MagicTex_ResolveByTex0"),
    0xCCB9D0: ("maybe_g_ffxTexSkipResident", "while set FFX_MagicTex_ResolveByTex0 skips kind 0 (resident) slots, 0x661286. The writer was not traced"),
    0x1940AB0: ("g_ffxMagicTexListRequested", "MagicFile__requestTexListOnce sets it, MagicFile__load clears it"),
    0x1940AB4: ("g_ffxMagicFileReused", "MagicFile__load sets it when the id asked for is the previous one still loaded (0x9DA52A), the poll then starts at once"),
    0x18716A0: ("g_ffxSpriteTexPathCount", "entries in g_ffxSpriteTexPaths, at most 10"),
    0x1875710: ("g_ffxSpriteTexPaths", "10 x 256 byte paths built by FFX_Magic_DrawSpriteTex0"),
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
