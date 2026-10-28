"""
Apply the menu layout script names from reversing/MENU_SCRIPT.md to the FFX.exe IDB.

menu_script.bin, system_script.bin and battle_script.bin are one format, read in place by a small
library at 0xA7B220..0xA7C8C0 (values looked up by name, scenes of keyframed 2D primitives played
inside the draw call). TOInit loads the three files into fixed buffers and registers them as slots
0, 1 and 2 of g_ffxMenuScriptSlots. The menu side wraps the library in getters that take a slot and
a name, and in a scene object table of 8 objects per slot. tools/menuscript.py reads the files.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all (the library and getter variants
with no caller have no function and no label, they are named with an _ORPHAN suffix). A name
starting maybe_ is a guess and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_menuscript_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the library, 0xA7B220..0xA7C8C0. a1 is always the slot 0..2 (0 system, 1 menu, 2 battle)
    0xA7C7F0: ("FFX_MenuScript_SetSlot", "g_ffxMenuScriptSlots[a1] = a2, the file buffer. TOInit and sub_8ADEE0 register 0 system_script, 1 menu_script, 2 battle_script. MENU_SCRIPT.md 2"),
    0xA7B7D0: ("FFX_MenuScript_FindValue", "(slot, name) -> value entry offset or 0. Bucket = xor of the name bytes & 15 at header +0, entry u32 = name offset (low 24 bits) | value count << 24, then the values. strcmp walk, steps 1 + count dwords"),
    0xA7B8C0: ("FFX_MenuScript_GetValueWord", "(slot, entry, i) -> u32 at file + entry + 4 + 4 * i. No bounds check on i"),
    0xA7B920: ("FFX_MenuScript_FindScene", "(slot, name) -> scene entry offset or 0. Same hash, buckets at header +0x40, 12 byte entries: name offset, track list | 0x80000000 running | 0x40000000 not done, s16 x, s16 y"),
    0xA7B220: ("FFX_MenuScript_DrawGroupScene", "(slot, group 0..15, scene entry, noRect). Runs the scene through FFX_MenuScript_RunScene when the draw group list at header +0x90 + 4 * group holds it. noRect = 1 skips op 4 rects only"),
    0xA7B460: ("FFX_MenuScript_RunScene", "(slot, scene entry, noRect). Only when the entry has bit 31. Steps and draws every track once, clears 0x40000000 when no track is alive. The animation advances here, once per draw call, which is inside FFX_MainStep: every menu step for slot 1, every non-catch-up step for slot 2 (MENU_DRAW_PATH.md 12), never per presented frame"),
    0xA7B510: ("FFX_MenuScript_StepTrack", "one track: count down, or run ops until one yields. Ops (word & 0x7FF): 0 end, 1 blank, 2 wait on track 0 (bug: compares with track 0's sequence only, 0xA7B5B7), 3 hide n, 4 rect, 5 line, 6 shape, 7 digits, 8 loop (0x800 forever), 9 loop start, 10. Lengths byte_C89C40"),
    0xA7B290: ("FFX_MenuScript_DrawTrack", "(seq base, track, counter, dur, noRect). dur < 0 draws the previous op, counter 0 the op at pc, else eases between the two keyframes when they are the same op, or snaps at t 0.5"),
    0xA7BBC0: ("FFX_MenuScript_DrawKeyframe", "lerp of two keyframes, value = (1 - t) * next + t * current, colours per byte. Flag 0x8000 adds the scene offset. op 4 -> FFX_MenuScript_DrawRectPrim, 5 -> nullsub_159 (lines are not drawn on PC), 6 -> DrawShapePrim, 7 -> DrawDigitsPrim, 10 -> DrawOp10Prim"),
    0xA7C6F0: ("FFX_MenuScript_Ease", "(word, counter, dur) -> t = counter / dur shaped by word & 0x7000: 0x2000 1-(1-t)^2, 0x3000 t^2, 0x4000 (cos((1-t) pi) + 1) / 2, else linear. dur 0 -> 1.0"),
    0xA7BA70: ("FFX_MenuScript_ResetScene", "(slot, scene entry, arm). Clears the two run bits on the entry, then per track sets or clears bit 31 and zeroes pc, counter, loop state, sync = -1. The tracks live in the loaded file, so the buffer is run time state"),
    0xA7C880: ("FFX_MenuScript_StartScene", "ResetScene(arm 1), then entry |= 0xC0000000"),
    0xA7C8A0: ("FFX_MenuScript_StopScene", "ResetScene(arm 0), then entry &= 0x3FFFFFFF"),
    0xA7BB90: ("FFX_MenuScript_GetSceneIfDone", "(slot, scene entry) -> entry pointer when 0x40000000 is clear (no track alive), else 0"),
    0xA7C810: ("FFX_MenuScript_SetSceneOffset", "(slot, scene entry, x, y) writes s16 entry +8 and +10, added to keyframes with flag 0x8000"),
    0xA7B360: ("FFX_MenuScript_DrawGroup_ORPHAN", "(slot, group). Runs every scene in the group with noRect 0, no scene object needed. No caller, no function"),
    0xA7B7A0: ("FFX_MenuScript_GetFirstValue_ORPHAN", "(slot, entry) -> first value. No caller, no function"),
    0xA7B890: ("FFX_MenuScript_GetValueWord_ORPHAN", "second copy of FFX_MenuScript_GetValueWord with a sign test instead of the slot clamp. No caller, no function"),
    0xA7B8F0: ("FFX_MenuScript_GetEntryPtr_ORPHAN", "(slot, offset) -> file + offset. No caller, no function"),
    0xA7B9D0: ("FFX_MenuScript_GetSceneOffsetX_ORPHAN", "(slot, scene entry) -> s16 entry +8. No caller, no function"),
    0xA7BA00: ("FFX_MenuScript_GetSceneOffsetY_ORPHAN", "(slot, scene entry) -> s16 entry +10. No caller, no function"),
    0xA7BA30: ("FFX_MenuScript_AdvanceTrackPc_ORPHAN", "(ops, track, op, keepPrev) pc += byte_C89C40[op]. No caller, no function"),
    0xA7BB60: ("FFX_MenuScript_IsSceneRunning_ORPHAN", "(slot, scene entry) -> entry when bit 31 is set. No caller, no function"),
    # primitives the keyframes draw
    0x8FDF20: ("FFX_MenuScript_DrawRectPrim", "op 4: FFX_Sprite2D_Draw from (x, y, colour0) to (x + w, y + h, colour1), PS2 units. Moves y 7 to 67 when battle scene bwakk is held and h is 22"),
    0x8FE0B0: ("FFX_MenuScript_DrawShapePrim", "op 6: shape id at +20 drawn at x, y with +16 / +18 as x / y percent scale. ids 200..399 and 400..599 index two shape tables, 411, 412, 415, 419, 507 draw nothing, 417 and 240 are HD special cases"),
    0x8FE080: ("FFX_MenuScript_DrawDigitsPrim", "op 7: draws the string 123456789 at x, y. No shipped sequence uses op 7"),
    0x8FE040: ("FFX_MenuScript_DrawOp10Prim", "op 10: sub_8928B0(s16 +28, x, y). No shipped sequence uses op 10"),
    # the menu side getters. Each takes (slot, name, out...) and writes zeros when the name is missing
    0x8AE360: ("FFX_MenuLayout_GetWord", "(slot, name, out u32). Colours and words"),
    0x8AE3A0: ("FFX_MenuLayout_GetBytes", "(slot, name, out b0, b1, b2, b3), a colour split into bytes"),
    0x8AE410: ("FFX_MenuLayout_GetShort", "(slot, name, out s16), the num_ values"),
    0x8AE450: ("FFX_MenuLayout_GetPoint", "(slot, name, out x, out y), u16 each"),
    0x8AE4B0: ("FFX_MenuLayout_GetRect", "(slot, name, out x, y, w, h), u16 each"),
    0x8C2AD0: ("FFX_MenuLayout_MenuWord", "FFX_MenuLayout_GetWord(1, name), slot 1 = menu_script.bin"),
    0x8C2AF0: ("FFX_MenuLayout_SlotWord", "FFX_MenuLayout_GetWord(slot, name), returns it"),
    0x8C2B10: ("FFX_MenuLayout_MenuShort", "FFX_MenuLayout_GetShort(1, name), returns it sign extended"),
    0x8C2B30: ("FFX_MenuLayout_SlotShort_ORPHAN", "FFX_MenuLayout_GetShort(slot, name). No caller, no function"),
    0x8C2B50: ("FFX_MenuLayout_MenuPoint", "FFX_MenuLayout_GetPoint(1, name, out, out + 2)"),
    0x8C2B70: ("FFX_MenuLayout_SlotPoint_ORPHAN", "FFX_MenuLayout_GetPoint(slot, name, out, out + 2). No caller, no function"),
    0x8C2B90: ("FFX_MenuLayout_MenuRect", "FFX_MenuLayout_GetRect(1, name, out x, y, w, h)"),
    0x8C2BC0: ("FFX_MenuLayout_SlotRect", "FFX_MenuLayout_GetRect(slot, name, out). Only caller is the orphan FFX_MenuLayout_LoadPanelSlot_ORPHAN"),
    0x8C1A60: ("FFX_MenuLayout_LoadPanel", "(out 24 bytes, rect name, colour0 name, colour1 name, a, b): rect at +0, colours at +8 and +12, a and b at +16 and +20 (plate part ids, 204/205 for config). 26 callers"),
    0x8C1AA0: ("FFX_MenuLayout_LoadPanelSlot_ORPHAN", "FFX_MenuLayout_LoadPanel with a slot argument. No caller, no function"),
    0x8AE6F0: ("FFX_MenuLayout_LoadCommon", "reads 63 values from slot 1 (window, nameplate, command, help and place plates, menu background colours) and 27 from slot 2 into globals. TOInit, sub_8ADEE0 (return to title) and both international swaps"),
    # the scene objects: 8 per slot, 32 bytes, at g_ffxMenuSceneObjects + slot * 0x100.
    # +0 u16 state 0 free 1 start 2 playing 3 held 4 stop, +2 u16 protected, +4 u16 hold, +8 name, +0x1C scene entry
    0x8ADF20: ("FFX_MenuScene_ClearAllObjects", "zeroes state and name of all 24 scene objects. TOInit and return to title"),
    0x8ADF80: ("FFX_MenuScene_ClearSlotObjects", "zeroes the 8 scene objects of a slot. FFX_MenuSys_Enter(1)"),
    0x8AF0D0: ("FFX_MenuScene_Start", "(slot, name). Refuses a name already present, takes a free object, FFX_MenuScript_FindScene, state 1. Auto stops when the scene finishes"),
    0x8AF1B0: ("FFX_MenuScene_StartHeld", "FFX_MenuScene_Start plus hold = 1, the scene keeps drawing its last frame (state 3) instead of stopping"),
    0x8AF2A0: ("FFX_MenuScene_StartProtected_ORPHAN", "FFX_MenuScene_Start plus protected = 1 (dword 0x10001), immune to StopUnprotected. No caller, no function"),
    0x8AF4F0: ("FFX_MenuScene_Stop", "(slot, name) sets a live object with that name to state 4"),
    0x8AF060: ("FFX_MenuScene_SetOffset", "(slot, name, x, y) -> FFX_MenuScript_SetSceneOffset for the object with that name"),
    0x8AE0F0: ("FFX_MenuScene_StepObjects", "(slot). 1 -> StartScene, state 2. 2 -> 3 when held, -> 4 when GetSceneIfDone. 4 -> StopScene, free. Then compacts the 8 objects. FFX_Module_StepAll (slot 1), FFX_BtlMenu_Step (slot 2)"),
    0x8AE2F0: ("FFX_MenuScene_DrawGroup", "(slot, group, noRect). FFX_MenuScript_DrawGroupScene for every object in state 2 or 3"),
    0x8AE340: ("FFX_MenuScene_DrawGroupRects", "FFX_MenuScene_DrawGroup(slot, group, 0). FFX_MenuObj_DrawAllAtPrio(prio) calls it with slot 1, group = prio"),
    0x8AE070: ("FFX_MenuScene_IsDone", "(slot, name) -> FFX_MenuScript_GetSceneIfDone != 0. Only slot 2 callers, the overdrive minigames"),
    0x8AE530: ("FFX_MenuScene_GetState", "(slot, name) -> the object's state word, 0 when absent. Only slot 2 (bwakk)"),
    0x8AE5A0: ("FFX_MenuScene_StopAll", "(slot) every object in state 1..3 -> 4. Only slot 2 callers"),
    0x8AEEC0: ("FFX_MenuScene_StopUnprotected", "(slot) every unprotected object in state 1..3 -> 4"),
    0x8AF380: ("FFX_MenuScene_StopPlayingUnprotected", "(slot) every unprotected object in state 2 or 3 -> 4"),
    0x8C2BF0: ("FFX_MenuScene_StartMenu", "FFX_MenuScene_Start(1, name)"),
    0x8AC8B0: ("FFX_MenuRes_GetSystemScriptBuf", "returns g_ffxSystemScriptBuf. No caller"),
    0x8AC8A0: ("FFX_MenuRes_GetMenuScriptBuf", "returns g_ffxMenuScriptBuf. No caller"),
    0x8AC890: ("FFX_MenuRes_GetBattleScriptBuf", "returns g_ffxBattleScriptBuf. No caller"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0xA7BB00: ("FFX_MenuScript_ClearSlots", "was __cfltcvt_init_150. a bogus library signature match. Zeroes the 8 slots of g_ffxMenuScriptSlots. TOInit and sub_8ADEE0"),
    0xA7B910: ("FFX_MenuScript_GetSlotBase", "was ?_Get_future_error_what@std@@YAPBDH@Z_16. a bogus library signature match. Returns g_ffxMenuScriptSlots[a1]. No caller"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x22FB4DC: ("g_ffxMenuScriptSlots", "8 file pointers. 0 system_script.bin (never read after load), 1 menu_script.bin, 2 battle_script.bin. FFX_MenuScript_SetSlot"),
    0x22FB4FC: ("g_ffxMenuScriptSceneOffsetX", "s16 scene entry +8 of the scene being run, added to keyframes with flag 0x8000"),
    0x22FB500: ("g_ffxMenuScriptSceneOffsetY", "s16 scene entry +10, see g_ffxMenuScriptSceneOffsetX"),
    0x185F610: ("g_ffxSystemScriptBuf", "0x800 bytes, jppc/menu/system_script.bin (path table kind 18 entry 20), loaded by TOInit, slot 0. Nothing reads slot 0"),
    0x185FE10: ("g_ffxMenuScriptBuf", "jppc/menu/menu_script.bin (kind 18 entry 21), loaded by TOInit for every language, slot 1"),
    0x1841E10: ("g_ffxBattleScriptBuf", "menu/battle_script.bin (kind 18, or 4 for languages 9 and 17, entry 22), loaded by TOInit and the international swaps, slot 2"),
    0x1865F20: ("g_ffxMenuSceneObjects", "3 slots x 8 objects x 32 bytes: +0 u16 state, +2 protected, +4 hold, +8 name[20], +0x1C scene entry offset"),
    0xC89C40: ("g_ffxMenuScriptOpWords", "u8[16], the length in u16 words of each scene op: 2 2 2 2 10 10 12 12 2 2 16"),
    0x25D1670: ("g_ffxMenuNamePlateSpacing", "num_namepltspc from menu_script.bin, overwritten with 44 by the main menu draw sub_8E0BA0 every frame"),
    0x25D1640: ("g_ffxMenuCommandPlateSpacing", "num_compltspc from menu_script.bin, overwritten with 32 by the main menu draw sub_8E0BA0 every frame"),
    0x186A48C: ("g_ffxMenuConfigNamePlateCount", "num_cnameplt or num_cnameplt_nonhdd, stored by FFX_Menu_ConfigSelectRowTable. Nothing reads it"),
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
