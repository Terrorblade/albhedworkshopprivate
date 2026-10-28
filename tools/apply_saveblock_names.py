"""
Apply the save block names from reversing/SAVE_BLOCK_MAP.md to the FFX.exe IDB.

The save block is g_ffxSaveData 0x112CA90 .. 0x1133350, 0x68C0 bytes. Mapping every byte of it
turned up a set of one line getters and setters, the guide map's explored-cell writer, the staging
image's tail helpers and a few globals inside the block that were still placeholders. Field offsets
are block offsets, see tools/saveblock_fields.py for the layout as data.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all (the two orphan staging copies
at 0x6485A0 and 0x648110 have no function and no label). A name starting maybe_ is a guess and says
so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_saveblock_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # header getters and setters
    0x86A9A0: ("FFX_SaveData_GetEncounterGroundAttr", "returns byte +0x10, the ground attribute the controlled actor stands on. Rewritten every field step. SAVE_BLOCK_MAP.md 2.1"),
    0x88D5D0: ("FFX_SaveData_GetScriptByte2B", "returns byte +0x2B. core:250 writes it, nothing in retail reads it except core:249 (no script uses it) and the debug page sub_8807B0"),
    0x88EB40: ("FFX_SaveData_SetScriptByte2B", "byte +0x2B = a1, the core:250 setter"),
    0x86BCC0: ("FFX_SaveData_GetTopLoveParamChar", "index 0..7 of the largest LoveParam dword at +0x2C, ties keep the lower index. core:333"),
    0x873AE0: ("FFX_SaveData_GetWorldMapMarkerX", "word +0xB4, written per map by core:366. The save screen sub_8E2B60 draws the scene30 sprite at (x/2, y/2-16). Debug label IMapX"),
    0x873AF0: ("FFX_SaveData_GetWorldMapMarkerY", "word +0xB6, see GetWorldMapMarkerX. Debug label IMapY"),
    0x8A9C50: ("FFX_SaveData_GetPlaytime", "dword +0xBC, seconds. Advanced from the wall clock by FFX_Save_StepPlayTimeFromWallClock on the vsync thread"),
    0x86F060: ("FFX_SaveData_SetRikkuAltOutfit", "byte +0xD1 = (a1 != 0), core:404"),
    0x86C8A0: ("FFX_SaveData_GetScriptFlagD7", "byte +0xD7. Read by sub_7829B0 and core:446"),
    0x8715F0: ("FFX_SaveData_SetScriptFlagD7", "byte +0xD7 = (a1 != 0). core:445 (64 script uses) and Ev_LoadEventPackage"),
    0x86C4B0: ("FFX_SaveData_GetScriptWordEA", "word +0xEA. core:526 and FFX_SaMenu_GetSaveWordEA"),
    0x870FA0: ("FFX_SaveData_SetScriptWordEA", "word +0xEA = a1, core:525"),
    # party and conf region
    0x784F50: ("FFX_SaveData_GetBattleCount", "returns g_ffxBattleCount, dword +0x3D14"),
    0x787220: ("FFX_SaveData_GetBattleCount_2", "a second identical getter of g_ffxBattleCount, dword +0x3D14"),
    0x785D40: ("FFX_SaveData_SetInnateAbilityFlag", "sets or clears an ability bit in the kernel table 31 copy at +0x3D28. ids 0x3000..0x305F use words 0..5, ids 0x3060 and up use words 6..15. Pair of FFX_SaveData_TestInnateAbilityFlag"),
    0x906B30: ("FFX_SaveData_LearnAlBhedPrimer", "a1 0..25 sets that bit in the primer mask at +0x3D10 (trophy 1, trophy 28 when all 26), 255 sets all, -1 clears"),
    0x906B10: ("FFX_SaveData_GetAlBhedPrimerMask", "returns the 26 bit primer mask, dword +0x3D10"),
    0x7B2950: ("FFX_SaveData_AddYojimboMotivation", "dword +0x3DA4 = clamp(old + a1, 0, 255)"),
    0x7B2B20: ("FFX_SaveData_GetYojimboMotivation", "returns dword +0x3DA4"),
    0x7B2B30: ("FFX_SaveData_GetYojimboWord3DA8", "returns dword +0x3DA8, Btl 233"),
    0x7B2B40: ("FFX_SaveData_SetYojimboWord3DA8", "dword +0x3DA8 = a1, Btl 234"),
    0x7B0CD0: ("FFX_Btl_StepTidusOverdriveLearn", "counts Tidus's commands in dword +0x3DAC and flags the next overdrive whose threshold in a0a0b0c0 is reached and whose event flag is not yet set"),
    # staging image
    0x8B3630: ("FFX_SaveMgr_GetStagingTail", "returns staging +0x64F8, block +0x64B8, the unused 0x408 byte tail. Stored at SaveMgr+0x35C by the ctor, which nothing reads"),
    0x8B3610: ("FFX_SaveMgr_ClearStagingTail", "memsets 0x400 bytes of the staging tail"),
    0x6485A0: ("FFX_SaveFile_StagePlayerPos_ORPHAN", "copies the cached player position flt_CCB9A8..B0 into the staging image +0xDC..+0xE4, block +0x9C. No caller"),
    0x648110: ("FFX_SaveFile_RestorePlayerPos_ORPHAN", "copies staging image +0xDC..+0xE4 back into the player position cache flt_CCB9A8..B0. No caller"),
    # guide map
    0x91F420: ("FFX_GuideMap_MarkExplored", "sets explored bits around (a2, a3) in the 64 x 64 bitmap at a1[28] (a save block record +0x350C..+0x3D0C), 40 unit cells, a 13 cell cross. Runs every step the guide map draws"),
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
    0xCCB9A8: ("g_ffxSavePlayerPosX", "float, the player position cached every step by FFX_MainStep_StepSaveScanAndCachePlayerPos and written into the save image at block +0x9C"),
    0xCCB9AC: ("g_ffxSavePlayerPosY", "float, see g_ffxSavePlayerPosX, block +0xA0"),
    0xCCB9B0: ("g_ffxSavePlayerPosZ", "float, see g_ffxSavePlayerPosX, block +0xA4"),
    0xC322A8: ("maybe_g_ffxBgMovieWord", "id | flag << 8, set by sub_643AA0 from FFX_Premovie_Load and sub_670760. maybe_FFX_Scene_Init saves it + 1 at block +0xAA"),
    0x22FD6C0: ("g_ffxShadowFlagPtrA", "FFX_Ch_ShadowsEnabled stores &g_ffxSaveData+0xD2 here. Nothing reads it"),
    0x23C4450: ("g_ffxShadowFlagPtrB", "FFX_Ch_ShadowsEnabled stores &g_ffxSaveData+0xD2 here. Nothing reads it"),
    0x11307A0: ("g_ffxAlBhedPrimerMask", "g_ffxSaveData+0x3D10, 26 bits, one per primer"),
    0x11307DC: ("g_ffxRngSeedBase", "g_ffxSaveData+0x3D4C, a conf template value. FFX_InitNewSaveData seeds the RNG streams from it plus sub_887E10()"),
    0x1130834: ("g_ffxYojimboMotivation", "g_ffxSaveData+0x3DA4, 0..255"),
    0x1130838: ("g_ffxYojimboWord3DA8", "g_ffxSaveData+0x3DA8, Btl 233 and 234"),
    0x113083C: ("g_ffxTidusOverdriveLearnCount", "g_ffxSaveData+0x3DAC, FFX_Btl_StepTidusOverdriveLearn"),
    0x1130840: ("g_ffxRikkuStealCount", "g_ffxSaveData+0x3DB0, successful steals, trophy 4 at 200"),
    0x1130844: ("g_ffxGilSpentCmd302A", "g_ffxSaveData+0x3DB4, gil paid through command 0x302A, trophy 19 at 100,000"),
    0x159E468: ("g_ffxSaveFileStagingTail", "g_ffxSaveFileStaging+0x64F8, the image of block +0x64B8, outside the CRC. FFX_SaveMgr_GetStagingTail"),
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
