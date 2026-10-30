"""
Apply the names from the 2026-10-29 texture animation port (Project Zanarkand src/engine/texanim,
PROGRESS 6.3.8) to the FFX.exe IDB.

The map and rcBg half of TextureAnimationManager: the script side setters the step calls, the
sampler and capture write applyFrames ends in, the singleton's teardown, and the load helpers
TextureAnimationManager__loadFromClusters walks with. TEXTURE_ANIMATION_AND_POST_TWEAKS.md part 1 has
the write up. Every name resting on use rather than on the instructions carries maybe_.

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

0x6521D0 has no function. It is an orphan copy of the two loads in FFX_Gfx_FinishMapLoad, nothing
references it, and the row only names the address. Create the function first if you want one.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_texanim_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x643A00: ('FFX_TexAnim_SetLayerAutoplay', '(layer, on): TextureAnimationManager__get then TextureAnimationManager__setLayerAutoplay(layer, on != 0). rcbgSceneStepLayers turns every layer off with it each sub step and genk05 layer 2 and genk12 layer 3 back on. [V]'),
    0x643A20: ('FFX_TexAnim_SetLayerFrame', '(layer, frame): TextureAnimationManager__setFrame(layer, frame, 0), the index relative to manager +8 + +12. rcbgSceneStepLayers passes the layer record +160 -> +60. [V]'),
    0x643A40: ('FFX_TexAnim_SetFrameByName', '(frame, id): TextureAnimationManager__findMaterialIndexById, 0 when none, else TextureAnimationManager__setFrame(index, frame, 1). A failed setFrame prints "SceneName.%s, TextureAnimName.%s." and gives 0. FFX_Map_ApplyTexAnimRequest keeps the request pending on 0. [V]'),
    0x6B3600: ('TextureAnimationManager__setLayerAutoplay', '(layer, on): index = +8 + +12 + layer, inside +116 the material\'s info (findInfo on the m_texAnimID PString at material +16) gets +28 = on. [V]'),
    0x642D90: ('FFX_TexAnim_SetLayerBase', '(base): manager +8 = base, the layer base every relative setFrame, setLayerAutoplay and getLayerMaterial adds. FFX_RcBg_SelectScene 0x921873 is the only caller. [V]'),
    0x6B20A0: ('TextureAnimationManager__findMaterialIndexById', '(id): the first index of +120 whose material +16 PString equals id (strcmp), -1 for none. [V]'),
    0x6B2070: ('TextureAnimationManager__getLayerMaterial', '(layer): the material at +8 + +12 + layer, null past +116. FFX_TexAnim_SetLayerAlpha and FFX_Gfx_SetRcBgLayerAlpha. [V]'),
    0x90BDB0: ('FFX_Map_ApplyTexAnimRequest', '(work, track, frame): the record at work + 1824 track + 28 frame + 645616 names the material id, sprintf "%05d_%02d_%03d_%03d" of +36 low 16, +36 top byte, +52 low 16 and (+52 >> 16) & 0x7FF, then FFX_TexAnim_SetFrameByName(frame, id). [V]'),
    0x90BF10: ('maybe_FFX_Map_UpdateScenePass', '(work, pass). FFX_Map_DrawPass0 and FFX_Map_DrawPass1. Scene flags and per layer values through sub_6423D0, sub_6426C0, sub_6426A0, and on pass 0 the track loop that calls FFX_Map_ApplyTexAnimRequest for every pending bit and clears it (*v7 ^= 1) when that gives nonzero. [V] for the texture animation half, [I] name'),
    0x6B3F40: ('TextureAnimationManager__initSamplerStates', 'Fills the two PSamplerStates applyFrames writes into a TextureSampler capture: +20 min 5, mag 1, wrap 1 1 1, and +56 min 5, mag 1, wrap 2 2 2, each unless its +0x80 flag is set, then sub_58CF80. The draw never binds them, PShaderProgramD3D11__bindCapturedTextures binds only the texture of a type 66 capture. [V]'),
    0x66E4F0: ('FFX_Material_WriteTextureCapture', '(definition, capture): 5 unless the definition type (+2) is 66, 217..220, 18, 67 or 68. Copies {type, index, sampler, texture} and turns a type other than 66..68 into 66 (67 or 68 when the definition is), then PParameterBuffer__setParameter. sub_59E520 always gives 0 so the write always happens. [V]'),
    0x56CBF0: ('maybe_PEffectVariant__findParameter', '(name): the first 16 byte parameter definition whose +4 PString equals name, searching the +32/+36 array then the +40/+44 one, 0 for none. applyFrames looks up "TextureSampler" with it on the material\'s m_effectVariant, FFX_Gfx_SetRcBgLayerAlpha "MaterialColour". [V] body, [I] class'),
    0x4D57D0: ('PParameterBuffer__setParameter', '(definition, src): 1 for a null src, 3 when offset (+8) < 4 or offset + (size (+10) & 0x1FFF) > the buffer size at +0, else memcpy into this + offset and 0. [V]'),
    0x6AF0D0: ('TextureAnimationInfo__copyCtor', 'Copies a TextureAnimationInfo: vtable, name duplicated, frame ids copied (count & 0x7FFFFFFF), +16 interval, +20 accumulator, +24 frame, +28 autoplay. [V]'),
    0x464F30: ('maybe_PClusterObjectIterator__seek', 'Moves the iterator (+20 end, +24 current) to the next object its filter sub_4659E0 accepts and loads it through sub_441DC0, or clears it. TextureAnimationManager__loadFromClusters seeks each texture row to its first PTexture2D with it. [I]'),
    0x6B2230: ('maybe_PSpriteAnimationInfoIterator__seek', 'The same walk as maybe_PClusterObjectIterator__seek with the sub_6B28F0 filter, which TextureAnimationManager__loadFromClusters uses over the .ags cluster. [I]'),
    0x6B24E0: ('maybe_PArray__appendInto', '(newStorage, value): copies the count (& 0x7FFFFFFF) old elements into newStorage, appends value, frees the old storage when it is owned (count >= 0), count + 1. [V] body, [I] name'),
    0x6582E0: ('FFX_Gfx_SetRcBgLayerAlpha', '(layer, alpha), this the gfx context: with +2364 set and an rcBg model (+4392), the layer\'s material (TextureAnimationManager__getLayerMaterial) gets MaterialColour = (1, 1, 1, alpha / 128). [V]'),
    0x644120: ('maybe_FFX_TexAnim_SetLayerAlpha', '(layer, value): TextureAnimationManager__getLayerMaterial(layer), then sub_66D5C0(material, value) when there is one. rcbgSceneStepLayers passes 0 for a layer with +0 & 3, FFX_RcBg_HideLayer and FFX_RcBg_ShowLayer. [V] for the call, [I] name'),
    0x6B0120: ('TextureAnimationManager__destroySingleton', 'From FFX__graphicFinalize: two sub_59C770 calls, the scalar deleting destructor of g_textureAnimationManager, then the pointer to 0. [V]'),
    0x641D90: ('maybe_FFX_Gfx_GetSceneName', 'sub_65DE70(g_ffxGfxCtx). rcbgSceneStepLayers runs strstr "genk12" and "genk05" on it, FFX_TexAnim_SetFrameByName prints it as SceneName. [I]'),
    0x6521D0: ('FFX_Gfx_LoadMapTextureAnimations_unused', 'No function and no xref. The same two TextureAnimationManager__loadFromClusters calls as FFX_Gfx_FinishMapLoad 0x65BC81..0x65BCE4: header +0x10E4 with model +0x10D4 and clusters +0x10D8, then header +0x1144 with model +0x1128 and clusters +0x112C. Hex-Rays loses the ecx of the second PAssetHeader__symbol in the real copy, this one shows it. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
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
