"""
Apply the PC save file names from reversing/SAVE_FILE.md to the FFX.exe IDB.

The PC save layer is three pieces, all on the main (step) thread:

    the save manager      g_ffxSaveMgr 0xCE81F4, a 0x488 byte object built at boot by
                          FFX_SaveSys_Init. +8 is the operation mode the save UI polls, +848 the
                          staging image pointer (g_ffxSaveFileStaging), +852 its size 26,880
    the slot state machine FFX_SaveFile_StepSlotStateMachine 0x6F0950, 27 states in
                          g_ffxSaveFileState 0xCE72E8, stepped once per FFX_MainStep. Save 1..6,
                          load 11..17, Al Bhed primer import 21..27. All file I/O is _wfopen on
                          <Documents>\\Square Enix\\FINAL FANTASY X&X-2 HD Remaster\\FINAL FANTASY X\\ffx_NNN
    the autosave          maybe_FFX_Scene_Init snapshots the staging image into g_ffxAutosaveImage,
                          the next player position update writes it to ffx_000

Four tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder
    NOTES    {ea: comment}                 a repeatable comment only, for functions that keep their
                                           name but whose comment is missing something. Only with
                                           --comments

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

    python apply_savefile_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the save manager object, g_ffxSaveMgr 0xCE81F4
    0x6F1C10: ("FFX_SaveMgr_Create", "FFX_Alloc(0x488), FFX_SaveMgr__ctor, stores g_ffxSaveMgr. From FFX_SaveSys_Init"),
    0x6F1980: ("FFX_SaveMgr__ctor", "+0 1, +8 mode -1, +12 -1, the console title / description / ICON0.png / empty.PNG strings at +16 +80 +208 +720 +784"),
    0x6F2070: ("FFX_SaveMgr_Get", "returns g_ffxSaveMgr. About 40 callers"),
    0x6F1ED0: ("FFX_SaveMgr__getStagingImage", "returns this+848, which FFX_SaveMgr__registerStaging set to g_ffxSaveFileStaging"),
    0x6F1FA0: ("FFX_SaveMgr__registerStaging", "this+848 = image, this+852 = size (26880), mode 0, mallocs the autosave image g_ffxAutosaveImage of the same size"),
    0x6F1AA0: ("FFX_SaveMgr__finishOp", "mode 3 -> 4 (load done), 1 -> 2 (save done), 5 -> 6. FFX_SaveUi_Step polls the mode"),
    0x6F1D70: ("FFX_SaveMgr__beginSave", "mode 1, then FFX_SaveFile_BeginSave(this+852, this+848)"),
    0x6F2410: ("FFX_SaveMgr__copyStagingToAutosave", "memcpy(g_ffxAutosaveImage, staging, 26880)"),
    0x6F1C40: ("FFX_SaveMgr__writeAutosave", "returns false when the staging header place name id (+0x18) is 27. Else copies it into the autosave image +0x18 and FFX_SaveFile_WriteToDisk(26880, g_ffxAutosaveImage), which is ffx_000"),
    0x6F2090: ("maybe_FFX_SaveMgr__isBusy", "0 for modes -1, 0, 2, 4, 6..10, else 1"),

    # the free functions around it
    0x647E10: ("FFX_SaveFile_RunAutosave", "FFX_SaveMgr__writeAutosave(mgr) - 1, so 0 is success. From FFX_Atel_PullActorPosFromChr when g_ffxCheckpointAutosavePending"),
    0x6484D0: ("FFX_SaveFile_SnapshotForAutosave", "FFX_SaveMgr__copyStagingToAutosave. From maybe_FFX_Scene_Init right after FFX_SaveFile_StageAndDescribe"),
    0x648540: ("FFX_SaveFile_FinishOp", "FFX_SaveMgr__finishOp(code). With code 3 and no primer import mask also restores the checkpoint music from staging block +0xA8 / +0xAA (dword_C526D0, sub_643AA0)"),
    0x648760: ("FFX_SaveUi_BeginLoad", "clears g_ffxSaveFileStagingVerified, mode 3, FFX_SaveFile_BeginLoad(26880, g_ffxSaveFileStaging)"),
    0x6487A0: ("FFX_SaveUi_BeginSave", "sets staging block +0xA8 = 0xFFFF and +0xAA = 0, FFX_SaveMgr__beginSave (which copies the image), then restores both words. So every manual save has those two words blanked, the autosave does not"),
    0x6480A0: ("FFX_SaveUi_IsStagingVerified", "g_ffxSaveFileStagingVerified == 1, i.e. staging header +4. FFX_SaveFile_VerifyCrc sets it, a failed check leaves it 0 and the load is not committed"),
    0x648090: ("FFX_SaveUi_IsLoadDone", "save manager mode == 4"),
    0x6480F0: ("FFX_SaveUi_IsSaveDone", "save manager mode == 2"),
    0x787370: ("FFX_Lang_DiffersFromRunning", "lang != FFX_GetLanguage(). Used on save header +0x0C"),

    # the slot state machine and its states
    0x6F0950: ("FFX_SaveFile_StepSlotStateMachine", "27 state switch on g_ffxSaveFileState, stepped by FFX_MainStep_StepSaveScanAndCachePlayerPos. Save 1 scan, 2 pick, 3 confirm, 4 write, 5 wait, 6 done. Load 11 scan, 12 pick, 13 confirm, 14 read, 15 verify CRC (fail: memset buffer 0, message 0x208) and language (message 0x209), 16 wait, 17 done. Primer import 21..27"),
    0x6F0330: ("FFX_SaveFile_EnsureSaveFolder", "SHGetFolderPathW(CSIDL_PERSONAL) + \\Square Enix\\ + FINAL FANTASY X&X-2 HD Remaster\\ + FINAL FANTASY X\\, creating each level, into g_ffxSaveFolderPath. Once, gated on g_ffxSaveFolderReady"),
    0x6F07A0: ("FFX_SaveFile_BeginSave", "FFX_SaveFile_StampCrc(src), mallocs g_ffxSaveFileOpBuffer and copies the image in, state 1, op in flight"),
    0x6F01D0: ("FFX_SaveFile_BeginLoad", "g_ffxSaveFileOpBuffer = the caller's buffer (the staging image), state 11, op in flight"),
    0x6EFEB0: ("FFX_SaveFile_BeginPrimerImport", "the Core 453 memory card check: buffer = staging, state 21, op in flight"),
    0x6F0A70: ("FFX_SaveFile_ScanSlots", "CreateFileW on ffx_000..ffx_199, records presence and the local LastWriteTime as yymmdd and seconds of day. ffx_000 does not count toward the 199 limit"),
    0x6F0FB0: ("FFX_SaveFile_SortSlotsByDate", "selection sort of slots 1..199 by write date then time, newest first. Slot 0, the autosave, stays first"),
    0x6F0C60: ("FFX_SaveFile_ListSlotHeaders", "fread of the first 0x40 bytes of each listed file: +5..+7 party icons, +0x10 playtime, +0x18 place name id, +0x20 name. No CRC check, the list shows corrupt files too"),
    0x6F0860: ("FFX_SaveFile_WriteSlotFile", "save state 4. Row 0 is a new slot (the first absent number 1..199), else the listed file. _wfopen wb, one fwrite of 26880, fclose. In place, no temp file, no flush"),
    0x6F0070: ("FFX_SaveFile_ReadSlotFile", "load state 14. _wfopen rb, fread 26880 into the staging image (return value ignored), header +0x0C against the running language into g_ffxSaveFileLanguageMismatch"),
    0x6EFD10: ("FFX_SaveFile_ImportPrimerFromSlot", "primer import state 24. Reads a whole save file into the stack, no CRC check, ORs its block +0x3D10 dword into the staging image and dword_1325AC4 / dword_1325B8C"),
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
    0xCE81F4: ("g_ffxSaveMgr", "the 0x488 byte save manager, FFX_SaveMgr_Create"),
    0xCE81F0: ("g_ffxAutosaveImage", "26880 byte malloc, a copy of the staging image taken at every qualifying scene init, written to ffx_000"),
    0xCE72E8: ("g_ffxSaveFileState", "FFX_SaveFile_StepSlotStateMachine state, 0 idle, 1..6 save, 11..17 load, 21..27 primer import"),
    0xCE72E4: ("g_ffxSaveFileDelayFrames", "30 frame wait after a read or a write"),
    0xCE7304: ("g_ffxSaveFileOpSize", "was ElementCount. 26880, the size every read and write uses. ElementCount was the fread parameter name leaking onto the global"),
    0xCE7308: ("g_ffxSaveFileOpBuffer", "the image being read or written. A malloc copy for a save, the staging image for a load or a primer import"),
    0xCE6EE0: ("g_ffxSaveFolderPath", "wchar[512], <Documents>\\Square Enix\\FINAL FANTASY X&X-2 HD Remaster\\FINAL FANTASY X\\"),
    0xCE72E0: ("g_ffxSaveFolderReady", "set once FFX_SaveFile_EnsureSaveFolder has built g_ffxSaveFolderPath"),
    0xCE7318: ("g_ffxSaveSlotList", "200 x {file number, yymmdd, seconds of day}, -1 when absent, sorted newest first from row 1"),
    0xCE7C78: ("g_ffxSaveSlotPresent", "200 dwords, 1 present, -1 absent, indexed by file number"),
    0xCE72F4: ("g_ffxSaveSlotFileCount", "files found by the scan, including ffx_000"),
    0xCE72F8: ("g_ffxSaveSlotRowCount", "rows the list shows"),
    0xCE72FC: ("g_ffxSaveSlotsFull", "199 or more manual slots exist, so the save list has no new row"),
    0xC3577C: ("g_ffxSaveFilePrefix", "-> \"ffx_\", formatted as %s%s%03d with the folder"),
    0xC35780: ("g_ffxSaveFolderPart0", "-> \"\\Square Enix\\\""),
    0xC35784: ("g_ffxSaveFolderPart1", "-> \"FINAL FANTASY X&X-2 HD Remaster\\\""),
    0xC35788: ("g_ffxSaveFolderPart2", "-> \"FINAL FANTASY X\\\""),
    0x1597F74: ("g_ffxSaveFileStagingVerified", "staging header +4. FFX_SaveFile_VerifyCrc writes 0 then 1 on success, FFX_SaveFile_StageAndDescribe writes 1"),
    0x1598058: ("g_ffxSaveFileStagingBgmA8", "staging block +0xA8, the checkpoint music word maybe_FFX_Scene_Init writes. FFX_SaveUi_BeginSave blanks it to 0xFFFF for the copy"),
    0x159805A: ("g_ffxSaveFileStagingBgmAA", "staging block +0xAA, written with sub_641400() + 1 at scene init. FFX_SaveUi_BeginSave blanks it to 0 for the copy"),
    0xC494CC: ("maybe_g_ffxSaveUiPs3ImportSlot", "-1 in .data. FFX_SaveUi_Step sets 0 when pad bit 0x80 is held in state 1, then walks 0..199 reading PS3Data/saves/<N> into staging and saving each through the PC writer. A converter for console saves, the folder does not exist in retail"),
}

# ---------------------------------------------------------------------------------------------
# Comments for functions that keep their name
# ---------------------------------------------------------------------------------------------

NOTES = {
    0x8B1450: "TABLE QUIRK (SAVE_FILE.md): the table loop is cmp ecx, 0FFh / jb at 0x8B1566, so entry 0xFF is never computed and stays 0 from the zero fill. It is not standard CRC-16/CCITT. Verified against 12 real saves, the standard table matches none",
    0x647D70: "On success the block +0x64B4 dword in the staging image is left ZERO (zeroed at 0x647DAE, never restored), so a block installed by FFX_SaveFile_CommitLoad has +0x64B4 = 0. Called by load state 15. The debug skip maybe_g_ffxSaveCrcSkip is only written by the orphaned FFX_DebugPage_basicInfo_updateInput_ORPHAN",
    0x646CA0: "CORRECTION (SAVE_FILE.md): not the PC slot list. Its only caller FFX_SaveFile_ReadSlotToStaging is reached from FFX_SaveUi_Step state 1 with maybe_g_ffxSaveUiPs3ImportSlot >= 0, the console save converter. The PC files are read by FFX_SaveFile_ReadSlotFile 0x6F0070",
    0x6F0510: "Writes ffx_000, the autosave, only. Caller chain FFX_Atel_PullActorPosFromChr -> FFX_SaveFile_RunAutosave -> FFX_SaveMgr__writeAutosave. Manual saves go through FFX_SaveFile_WriteSlotFile 0x6F0860",
    0x81FF80: "CORRECTION (SAVE_FILE.md): state 2 commits a load only when FFX_SaveUi_IsStagingVerified (staging +4 == 1), so a bad CRC is refused. WORLD_STATE.md used to say the PC load path never checks the CRC",
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
    for ea, text in sorted(NOTES.items()):
        print("note  0x%X %s%s" % (ea, _cur_name(ea) or "(no name)", ("    " + text) if verbose else ""))
        if not dry_run and comments and not _add_comment(ea, text):
            print("      note not added at 0x%X (already there, or it would pass 1000 characters)" % ea)
    print("NOTES: %d rows, applied only with --comments" % len(NOTES))
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
    ap.add_argument("--comments", action="store_true", help="also append each row's comment, and the NOTES")
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
