"""
Apply the Steam achievement names from reversing/STEAM_ACHIEVEMENTS.md to the FFX.exe IDB.

FFX.exe reaches Steam user stats through one class, StatsAndAchievements (the name comes from the
RTTI of its three CCallback members), built once by FFX_SaveSys_Init into dword_CE81A8 when
SteamAPI_Init succeeded. It carries a 34 row Achievement_t table at 0xC35790, 400 bytes a row, and
every unlock in the game goes FFX_Trophy_Unlock -> FFX_Ach_UnlockIfSteamReady ->
StatsAndAchievements__unlock -> SetAchievement + StoreStats. No Steam stat is ever read or written.
This names the class, its singleton plumbing, the trigger helpers that were still placeholders and
the latches and counters the triggers keep.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all (several of the class's methods
sit in function-less code with no caller and no label). A name starting maybe_ is a guess and says
so. A name ending _ORPHAN has no caller in the binary.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_achievement_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the Steam class, ISteamUserStats011 vtable slots from the SDK 1.31 header
    0x6F11A0: ("StatsAndAchievements__ctor", "registers the UserStatsReceived (1101), UserStatsStored (1102) and UserAchievementStored (1103) callbacks, CGameID from SteamUtils GetAppID at +0x3C, table 0xC35790 at +0x44, count 34 at +0x48, +0x4C stats received = 0, +0x4D enabled = 1, then RequestCurrentStats when SteamUser BLoggedOn. STEAM_ACHIEVEMENTS.md 2"),
    0x6F1370: ("StatsAndAchievements__dtor_ORPHAN", "unregisters the three callbacks. No caller"),
    0x6F13C0: ("StatsAndAchievements__scalar_deleting_dtor_ORPHAN", "dtor then FFX_Free when bit 0 of the flag. No caller"),
    0x6F1420: ("StatsAndAchievements__resetAllStats", "ISteamUserStats slot 21 ResetAllStats(true). Only the orphan debug page row 12 Achievements Reset calls it. Leaves the local achieved bytes set, so a reset is not seen until the next RequestCurrentStats"),
    0x6F1430: ("FFX_Ach_Create", "allocates the 0x50 byte StatsAndAchievements into dword_CE81A8. FFX_SaveSys_Init calls it only when g_ffxSteamApiReady"),
    0x6F14A0: ("FFX_Ach_Destroy_ORPHAN", "unregisters the callbacks, frees dword_CE81A8 and clears it. No caller, so the object lives until exit"),
    0x6F1510: ("StatsAndAchievements__unlock", "(this, row): when +0x4D and row <= 33 and the row's achieved byte (table + 400*row + 0x188) is 0, setAchievement(row) then unlockCompletionIfAllDone. So a row goes to Steam at most once while its local byte stays set"),
    0x6F1550: ("StatsAndAchievements__unlockCompletionIfAllDone", "when row 0 ACH_COMPLETION_OF_FFX is not achieved and rows 1..33 all are, unlock(0)"),
    0x6F15B0: ("FFX_Ach_Get", "returns dword_CE81A8, the StatsAndAchievements singleton"),
    0x6F15C0: ("FFX_Ach_AllOthersAchieved_ORPHAN", "1 when rows 1..33 are all achieved. No caller"),
    0x6F15E0: ("StatsAndAchievements__onAchievementStored", "UserAchievementStored_t (1103) handler, empty"),
    0x6F15F0: ("StatsAndAchievements__onUserStatsReceived", "UserStatsReceived_t (1101): when the game id matches and eResult is 1, +0x4C = 1 and for every row GetAchievement(api, &achieved) (slot 6) and GetAchievementDisplayAttribute name and desc (slot 12) into the row's 128 and 256 byte buffers"),
    0x6F16D0: ("StatsAndAchievements__onUserStatsStored", "UserStatsStored_t (1102) handler, empty"),
    0x6F17D0: ("StatsAndAchievements__requestStats_ORPHAN", "RequestCurrentStats when +0x4D, SteamUser exists and BLoggedOn. No caller"),
    0x6F18B0: ("StatsAndAchievements__setAchievementByName_ORPHAN", "SetAchievement(name) then StoreStats when +0x4D. No caller"),
    0x6F1900: ("StatsAndAchievements__setAchievement", "(this, row): achieved byte = 1, icon = 0, SetAchievement(api name) (slot 7) then StoreStats (slot 10). The only live SetAchievement in the binary"),
    0x6488C0: ("FFX_Ach_UnlockIfSteamReady", "FFX_Ach_Get()->unlock(row) when g_ffxSteamApiReady. The second half of FFX_Trophy_Unlock"),
    0x6480E0: ("maybe_FFX_SaveMgr_IsGameStarted", "returns SaveMgr+0. The ctor sets it to 1, sub_647E40 sets 1 on New Game and when FFX_SaveUi_Step state 2 takes a chosen slot, the only clearing thunk 0x647E30 has no caller. So the FFX_Trophy_Unlock gate is always open once the save manager exists"),
    0x647E40: ("maybe_FFX_SaveMgr_MarkGameStarted", "SaveMgr+0 = 1. Called by core:573 BeginNewGame and by FFX_SaveUi_Step state 2 when a slot was chosen"),
    0x647E30: ("maybe_FFX_SaveMgr_ClearGameStarted_ORPHAN", "SaveMgr+0 = 0 through a folded body. No caller"),
    # trigger sites
    0x822760: ("FFX_Ach_CheckBossKill", "a1 is the defeated unit's model number from its chr path (mNNN). 130 Yunalesca -> 10, 131 Seymour Omnis -> 11, 176 Yu Yevon -> 32, 276 Nemesis -> 31, 344 Penance -> 30"),
    0x88ECC0: ("FFX_Ach_CheckJechtSpheres_ORPHAN", "an uncalled copy of the trophy 20 check at the top of maybe_FFX_Scene_Init"),
    0x8B5680: ("FFX_Ach_CountScene6Confirms", "per message window that reaches its waiting state while g_ffxLiveSceneId == 398 (sc/scene6), count up. At 58, the number of messages in scene6, unlock 33 ACH_A_JOURNEY_CATALYST and park the count at -1"),
    0x78B760: ("FFX_Btl_ApplySteal", "steal handler from FFX_Btl_ApplyActionToTargets (ability flag 0x200). Rolls stream 10 against the chance and stream 11 for the rare drop, adds the item. A success by actor chr 6 (Rikku) bumps save +0x3DB0 and unlocks 4 at 200"),
    0x893290: ("maybe_FFX_BtlMenu_DrawDamagePopups", "from FFX_BtlMenu_DrawRoot, so only on drawn steps. Turns the latched g_ffxBtlDealt9999Flag and 99999Flag into unlocks 18 and 21 and clears them, then draws the queued number entries"),
    0x8C4E60: ("FFX_NameInput_CommitName", "naming screen state 5: writes the typed name into the save. Naming char 17 unlocks 15 and 26, char 13 unlocks 13, char 14 unlocks 14"),
    0x6FC7B0: ("FFX_EscMenu_ShowGrantPopup", "Esc menu button 13. Outside a game says Function not Available now, else a three button popup (text 713..715) that reports to FFX_EscMenu_OnGrantPopupResult"),
    0x6FC970: ("FFX_EscMenu_OnGrantPopupResult", "choice 0 FFX_Debug_FullItem, 1 unlocks 25 and 24 then FFX_Btl_LoadPlayerData (all abilities), 2 FFX_Debug_FullGil"),
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
    0xC35790: ("g_ffxAchievementTable", "Achievement_t[34], 400 bytes a row: +0 row id, +4 api name, +8 name[128], +0x88 desc[256], +0x188 achieved byte, +0x18C icon. Row 0 ACH_COMPLETION_OF_FFX is automatic. STEAM_ACHIEVEMENTS.md 3"),
    0xCE81A8: ("g_ffxStatsAndAchievements", "the 0x50 byte StatsAndAchievements singleton, null when SteamAPI_Init failed"),
    0xC58E8A: ("g_ffxAchJechtSphereLatch", "starts 1. Scene init with save +0xC7F == 8 clears it, == 9 with it clear sets it and unlocks 20. So a save loaded already at 9 does not unlock"),
    0xC5A03D: ("g_ffxAchTheaterArmed", "starts 1. FFX_MesWin_DrawInsertedValue clears it and unlocks 16 when save +0 == 158 and +0x34B == 50 and +0x34A == 71"),
    0x18663E4: ("g_ffxAchScene6ConfirmCount", "bss, 0 at boot. FFX_Ach_CountScene6Confirms, -1 once 33 unlocked. Never reset"),
    0xC3347C: ("g_ffxAchScene6Id", "constant 398, sc/scene6, compared by FFX_Ach_CountScene6Confirms"),
    0x112D6C9: ("g_ffxCelestialWeaponsFound", "g_ffxSaveData+0xC39, the seven Celestial weapons found bits (MINIGAMES_PROGRESSION.md 4.2). FFX_MainStep unlocks 27 when it goes from 1..126 to 127 within one process"),
    0x112D70F: ("g_ffxJechtSphereCount", "g_ffxSaveData+0xC7F, Jecht Spheres after the first (MINIGAMES_PROGRESSION.md 5). maybe_FFX_Scene_Init unlocks 20 on 8 then 9"),
    0x11307FC: ("g_ffxSaveSharedAbilityBits", "g_ffxSaveData+0x3D6C, u16[16], the ability bits for ids 0x3060 and up. Bits 8..19 all set after a Kimahri Lancet learn unlocks 25"),
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
