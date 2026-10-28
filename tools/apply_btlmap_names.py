"""
Apply the battle map names from reversing/BTLMAP_FORMAT.md to the FFX.exe IDB.

Reading how a battle loads its background (FFX_Btl_StepSubPhase cases 7, 8, 9, 34 and 10, the MAP1
loader and the Phyre scene load) and where the battle reads the walkmesh back (the ground height
consumers and the placement table of the battle field file) left a set of placeholders: the gfx
flag setters the map load uses, the pending MAP1 blob and path, the battle placement accessors, the
arc path builder and the battle UI projections that read the ground height.

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

    python apply_btlmap_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the map load the battle start drives
    0x643490: ("FFX_Gfx_SetBattleMapFlag", "gfx +4503 = old +4502, then +4502 = (a1 != 0). FFX_Btl_StepSubPhase case 8 passes 1 before the battle map load, FFX_Btl_Shutdown and maybe_FFX_Scene_Init pass 0. BTLMAP_FORMAT.md 3"),
    0x643450: ("FFX_Gfx_SetMapExpectsBgScene", "gfx +4304 = (a1 != 0), set 1 just before FFX_Gfx_FieldMapLoad(name, 0) when MAP1 section 0 is present. FFX_Gfx_FinishMapClusterSet waits for it with +4396. No btlmap has section 0"),
    0x643470: ("FFX_Gfx_SetMapExpects3dScene", "gfx +4305 = (a1 != 0), set 1 just before FFX_Gfx_FieldMapLoad(name, 1) when MAP1 section 1 is present. FFX_Gfx_FinishMapClusterSet waits for it with +4312. Every btlmap has section 1"),
    0x908F10: ("FFX_GetMagicDrawFlags", "returns g_ffxMagicDrawFlags. FFX_Btl_StepSubPhase case 9 waits while bit 0x20 or 0x40 is set. FFX_Map_LoadMapOutAsync sets 0xA0 and FFX_Map_OnMapOutLoaded clears it, so this is the MAP1 read completion gate"),
    0x908F40: ("FFX_Map_GetLoadedMapOutRow", "returns g_ffxMapOutLoadedRow, the row of the last MAP1 loaded through FFX_Map_LoadMapOutAsync (request +24). Read by FFX_Oef2_Op161 and two other effect functions"),
    0x90BBE0: ("FFX_Map_AllocAligned64", "FFX_Mem_SetNextAllocAlign(64) then FFX_MemAlloc(size, tag). The MAP1 buffer of both loaders"),
    0x6454A0: ("FFX_Map_RequestParticleTextures", "calls sub_664F60 (the %s%sfp/tex/TexList.txt request, gated on gfx +37) then sub_6C14C0. Called right after the Phyre scene loads start, so it fetches the fp/tex textures beside a map, 309 of them under btlmap"),
    0x7816A0: ("FFX_Btl_GetDbgMapCircle", "returns g_ffxBtlDbgMapCircle. FFX_Map_OnMapOutLoaded and FFX_Map_LoadMapOutSync pick -1 or -9 for dword_18DED0C from it"),
    0x784420: ("FFX_Btl_PlaceMonstersAtHome", "units 20..27: unhide the CHR and FFX_Ch_SetPos to the placement point from the battle field file (FFX_Btl_GetUnitPlacementPos). FFX_Btl_StepSubPhase case 10 calls it only when g_ffxBtlMapLoadIssued, so a battle staged on the current field map keeps the monsters where the field put them"),
    0x7ABFE0: ("FFX_Btl_AccessPlacementPoint", "(write, actor, layout, kind, slot, vec4). Reads (write 0) or writes the placement point of the battle field file section +16 (g_ffxBtlFieldPlacementSection): 96 byte layout records, kind 2..11 picks a fixed vec4 or a table at +16..+44. The battle positions come from here, never from the btlmap"),
    0x7A9AC0: ("FFX_Btl_ResolveMoveTargetPos", "switch on a move target mode, writes the target position. Mode 13 is the placement point with y = ground height - height/4, mode 14 the unit position with y = ground height (FFX_Btl_GetUnitGroundHeight). From FFX_Btl_StartUnitMove, FFX_Btl_StepUnitMove, Btl 013 and Btl 044. The walkmesh reaches battle movement here"),
    0x7B78C0: ("FFX_Btl_BuildArcPath", "builds the 8 point arc path of a unit move by actor +1056, the landing y is FFX_Btl_GetUnitGroundHeight unless g_ffxBtlFieldAirborne. Btl 118 and FFX_Btl_StepUnitMove"),
    0x795250: ("FFX_BtlUi_ProjectUnitToScreen", "projects the unit position, with y = ground height - actor +1260, to virtual screen space and stores it at actor +3924/+3928, clamped to the window. Called from the battle menu UI. Presentation"),
    0x796C30: ("FFX_BtlUi_ProjectUnitGroundToScreen", "projects the unit position with y = ground height minus a cached bone height to screen x, y. UI only"),
    0x7932B0: ("maybe_FFX_BtlUi_CacheUnitBoneHeight", "ground height - the y of the actor's +3577 bone, stored in the 12 byte UI table at dword_2311050. UI only"),
    0x937960: ("FFX_RcDev_ReadMapNameCsv", "dev tool: looks a map id up in host:/ffx/proj/map/btlmaster/info/btlmap.csv (8 chars) or master/info/mapid.csv (6 chars). The files do not ship, retail never reaches it"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7948E0: ("FFX_Btl_GetUnitPlacementPos", "was maybe_FFX_Btl_GetUnitTargetPos. Returns FFX_Btl_AccessPlacementPoint(0, actor, actor +1276 layout, +1277 kind, +1278 slot) plus the actor y offset +1332, so it is the unit's placement point from the battle field file"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x112C9DA: ("g_ffxBtlMapLoadIssued", "byte, 0 at FFX_Btl_BeginBattle, 1 in FFX_Btl_StepSubPhase case 8 when a battle map load is issued. Case 10 then runs FFX_Btl_PlaceMonstersAtHome and maybe_FFX_Btl_SetupUnitChr repositions existing CHRs"),
    0x112C9CE: ("g_ffxBtlFieldAirborne", "byte 3 of the battle field monster section, copied by FFX_Btl_SetupFieldPointers. Btl 005 returns it. Set means the unit code skips the ground height snaps"),
    0x112A9B0: ("g_ffxBtlFieldPlacementSection", "battle field file + *(u32*)(file + 16), the placement section read by FFX_Btl_AccessPlacementPoint and SG_DebugWin_BattlePosProc"),
    0x1934ED0: ("g_ffxMapOutPendingBlob", "MAP1 buffer of the async load in flight, set by FFX_Map_LoadMapOutAsync, read and cleared by FFX_Map_LoadPhyreScenesForMapOut"),
    0x1934EE8: ("g_ffxMapOutPendingPath", "char[], the cd path of the MAP1 in flight (FFX_Asset_GetPathForIndexChecked). FFX_Map_LoadPhyreScenesForMapOut hands it to FFX_Gfx_FieldMapLoad"),
    0x1934EC0: ("g_ffxMapSceneKind", "0 after an rcBg (section 0) scene load, 1 after a 3D (section 1) scene load"),
    0x1934EC4: ("g_ffxMapSceneLoadCount", "++ per FFX_Gfx_FieldMapLoad issued by the MAP1 loaders, read back by FFX_Map_UnloadMapOut"),
    0x18DED30: ("g_ffxMapOutLoadedRow", "row of the last MAP1 loaded async, from request +24 in FFX_Map_OnMapOutLoaded. FFX_Map_GetLoadedMapOutRow"),
    0xC3414C: ("g_ffxHdaoSettingsPaths", "two paths, PS3Data/map/hdao_settings.bin and PS3Data/btlmap/hdao_settings.bin, both loaded by sub_693D70 in one loop. Ambient occlusion settings, presentation"),
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
