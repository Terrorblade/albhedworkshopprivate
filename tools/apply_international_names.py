"""
Apply the International content names from reversing/INTERNATIONAL_CONTENT.md to the FFX.exe IDB.

Mapping the International switch, Eternal Calm, the Dark Aeons, Penance and the two sphere grids
turned up the environment swap's helpers and latches, the dead battle reload behind it, the grid
type's expert-only readers and the per type start tables. FFX_GetInternationalEnvId is a hardcoded
0 in this build, so most of what is named here is dead or only half run, and the comments say so.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_international_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the environment swap
    0x8B0340: ("TOSwapInternationalEnvExecSync", "the one call twin of TOSwapInternationalEnvExec: loads menu entries 11, 10, 15, 14, 8, 6, 5, 7, 22 of g_ffxIntlEnvMenuAssetKind, then FFX_Btl_ReloadForIntlEnvSync. Callers core:600 poll and FFX_IntlEnv_SyncOnReturnToTitle, both dead for the reload. INTERNATIONAL_CONTENT.md 4"),
    0x8B0920: ("FFX_IntlEnv_BeginSwap", "(env) FFX_SaveData_SetTextVariantBit(env), g_ffxIntlEnvMenuAssetKind = env == 1 ? 4 uspc menu : 18 jppc menu, resets the swap state, done, busy and step count. Called with env 0 on every PC load by FFX_SaveFile_CommitLoad"),
    0x8B0970: ("FFX_IntlEnv_BeginSwapMenu", "the TOSwapInternationalEnvExecMenu twin of FFX_IntlEnv_BeginSwap, sets dword_C59978. Only from FFX_Menu_ExecModule10Config states 6..8, which are dead"),
    0x785ED0: ("FFX_SaveData_SetTextVariantBit", "(a1) option dword block +0x3D0C bit 16: cleared when the text language is 1 or 18, else set to a1 & 1. Every PC call passes 0 except FFX_IntlEnv_SyncOnReturnToTitle (variant != 0) and the Config row sub_8CAFF0. No reader of bit 16 found through g_ffxSaveDataOptionFlags or FFX_SaveData_GetOptionFlagsPtr"),
    0x8AC390: ("FFX_IntlEnv_IsSwapEnabled", "returns g_ffxIntlEnvSwapEnabled, 1 on PC (text language 0, variant 0..2)"),
    0x8ACF20: ("FFX_IntlEnv_IsSwapBusy", "returns g_ffxIntlEnvSwapBusy. Read by FFX_Font_UploadMenuSheets, which skips two texture uploads while it is 1. It stays 1 after the first PC load"),
    0x86ECE0: ("FFX_IntlEnv_SyncOnReturnToTitle", "from FFX_Map_ReturnToTitle: FFX_SaveData_SetTextVariantBit(variant != 0) always, then a full TOSwapInternationalEnvExecSync only if the env id changed across the call, which it cannot"),
    0x8B3DB0: ("FFX_SaveUi_CloseAndSwapEnv", "closes the save/load UI (cancel, failed verify, save done). If the swap is enabled and g_ffxLoadIntlEnvSwapPending is 1 it restarts the swap and steps it once, then suspends module 16 or 15"),
    0x8B15D0: ("FFX_SaveLoadMenu_StepModule", "the menu save/load module step, in the module table off_C59FF0. States 104..113 install a block, then step TOSwapInternationalEnvExec to completion in state 112. The PC installs through FFX_SaveFile_CommitLoad instead"),
    # the dead battle reload
    0x782090: ("FFX_Btl_ReloadForIntlEnvStep", "(step) returns 0 at 0x7820AA when FFX_GetInternationalEnvId() == g_ffxBtlLoadedIntlEnvId. Both are 0, so the battle and kernel reload states below never run, which also kills the encounter roll path MENU_SYSTEM.md described"),
    0x782050: ("FFX_Btl_ReloadForIntlEnvSync", "loops FFX_Btl_ReloadForIntlEnvStep to completion, returns 0 at once in this build"),
    0x784E60: ("FFX_MesWin_TextLanguageIsNot1", "returns 0 when FFX_MesWin_GetTextLanguage() == 1, else -1"),
    # sphere grid
    # 0xA54AB0: ("FFX_SphereGrid_ReadPanelKernels", "selects rom kind 13 (env 0) or 7 and reads the sphere and panel kernel tables through FFX_Btl_ReadKernelBin. The same for every grid type"),  # dropped 2026-10-28, already FFX_SphereGrid_ReadPanelTables from tools/apply_kernel_table_names.py
    0xA57620: ("FFX_SphereGrid_LoadBoardEffect", "from FFX_Menu_Build. Expert ((option & 0xC000) == 0x8000) loads kind 37 index 20 jppc/menu/abmap/dat12.dat, else index 8 dat00.dat, into dword_11A1260 and hands it to FFX_Oef2_SetFromUnit"),
    0xA48910: ("FFX_SphereGrid_ActivateNodeForChar", "(char, node) effect 4 for Expert, 0 otherwise, then sets the node's activation bit, FFX_SphereGrid_MenuToSave, FFX_SphereGrid_RecomputeDerived and FFX_SphereGrid_CheckGridTrophies. The grid type only changes the effect"),
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
    0x1866240: ("g_ffxLoadIntlEnvSwapPending", "set to 1 by FFX_SaveFile_CommitLoad 0x8B4F43 and FFX_SaveLoadMenu_StepModule state 105, cleared only when the swap finishes. Stays 1 after a PC load"),
    0x1865CDC: ("g_ffxIntlEnvSwapEnabled", "set by FFX_MesWin_SelectFontForLanguage from TOInit, 1 when the text language is 0 and the variant 0..2, which is always on PC"),
    0x1841D28: ("g_ffxIntlEnvSwapState", "TOSwapInternationalEnvExec state 0..28, a plain dword although the decompiler shows a pointer"),
    0x1841D2C: ("g_ffxIntlEnvSwapDone", "1 when the swap reached state 28, what TOSwapInternationalEnvExec returns"),
    0x1841D30: ("g_ffxIntlEnvSwapBusy", "1 from swap state 0 until state 28. A PC load steps the swap once, so it stays 1. FFX_IntlEnv_IsSwapBusy"),
    0x1841D34: ("g_ffxIntlEnvSwapBtlStep", "the step counter the swap passes to FFX_Btl_ReloadForIntlEnvStep"),
    0xC59564: ("g_ffxIntlEnvMenuAssetKind", "asset kind for the swap's menu files, 18 _cdindex_menu (jppc) or 4 _cdindex_menu_inter_us (uspc). Set by FFX_IntlEnv_BeginSwap"),
    0x112CA34: ("g_ffxBtlLoadedIntlEnvId", "the env id FFX_Btl_Init stored. FFX_Btl_ReloadForIntlEnvStep compares against it and returns 0 when equal, always in this build"),
    0x186A43C: ("g_ffxMenuConfigEnvIdAtOpen", "FFX_Menu_ConfigInitRowValues stores the env id here (not the map id). Module 10 state 4 compares it, always equal"),
    0x186A1CC: ("g_ffxMenuSysEnvIdAtEnter", "FFX_MenuSys_Enter stores the env id here and compares it later, always equal. The function comment calling it a scene snapshot via sub_7851D0 means FFX_GetInternationalEnvId"),
    0xC416C4: ("g_ffxSaMenuMonGraKind", "sa_menu_start: 48 _cdindex_mon_gra when the env id is 0, else 3 _cdindex_mon_gra_inter_us"),
    0xC86C00: ("g_ffxSphereGridStartNodesType0", "WORD[7] start node per character, grid type 0, the menu.bin template value. INTERNATIONAL_CONTENT.md 7"),
    0xC86C10: ("g_ffxSphereGridStartNodesStandard", "WORD[7] start node per character, grid type 1 Standard"),
    0xC86C20: ("g_ffxSphereGridStartNodesExpert", "WORD[7] start node per character, grid type 2 Expert"),
    0xC86C5C: ("g_ffxSphereGridStartActivatedType0", "WORD *[7], 0xFFFF terminated pre-activated node lists, grid type 0"),
    0xC86CA4: ("g_ffxSphereGridStartActivatedStandard", "WORD *[7] pre-activated node lists, grid type 1 Standard"),
    0xC86CEC: ("g_ffxSphereGridStartActivatedExpert", "WORD *[7] pre-activated node lists, grid type 2 Expert"),
    0x1A85F60: ("g_ffxSphereGridArtLoaded", "FFX_SphereGrid_InitNodes loads the 70 eiichi_abmap_data files once, guarded by this"),
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
