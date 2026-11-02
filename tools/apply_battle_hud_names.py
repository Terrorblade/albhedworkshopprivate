"""
Apply the battle HUD names to the FFX.exe IDB (2026-11-01).

These come from item 6 of the battle port in Project Zanarkand (PROGRESS 13.11): the floating numbers' step
half (src/ffx/btl/FloatingNumbers.cpp), the party panel, the popup queue and the text helpers the HUD draws
through (src/ffx/btl/BattleHud.cpp). Each function was read in the decompile and the disassembly while porting
it. See reversing/BATTLE_MENU.md 3.12 and the 2026-11-01 lines in reversing/BATTLE_CTB_AND_STATUS.md and
reversing/MENU_DRAW_PATH.md.

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

    python apply_battle_hud_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The floating numbers, the step half. FFX_Btl_ShowFloatingNumber 0x79FA00 places the records
    0x79F5E0: ("FFX_Btl_StepFloatingNumbers", "(): every step from FFX_Btl_MainStep (0x790D88). Walks the record list from g_ffxBtlFloatingDigitHead: a delay above 0 counts down, the age counts up, a record past its life (30) is freed (byte 0 = 0) and unlinked. Then recounts the waits from the records kept: style & 0x7F 1 into 0x112C9D3 and 0x112C9D4, 2 into 0x112C9D4, every record into 0x112C9D5, so a number holds the action finish, ExecCommand and the end phase for its 31 steps. g_ffxBtlFloatingDigitTail = the last record kept. Simulation, not a visual"),
    0x79FC60: ("FFX_Btl_PlaceFloatingDigit", "(unit, index, glyph, x, y, w, h, life, colour, style): the first of the 128 records of 20 bytes at g_ffxBtlFloatingDigits whose byte 0 is 0, stored through the tail link, its own next (+16) the new tail. Delay index + 1, age 0. 1, or 0 when all are taken"),
    0x79FC00: ("FFX_Btl_SplitFloatingDigits", "(value, out): the digits of value, least significant first, at most nine. None for a negative value, one 0 for 0"),
    0x79F9B0: ("FFX_Btl_ResetFloatingNumbers", "(): every record of g_ffxBtlFloatingDigits free and the list emptied. From FFX_Btl_BeginBattle (0x7814C6)"),
    0x79F990: ("FFX_Btl_FreeFloatingNumberPool", "(): FFX_MemFree(g_ffxBtlFloatingDigits) when set, then 0. From FFX_Btl_Shutdown"),
    0x79F9F0: ("FFX_Btl_ClearFloatingNumberPool", "(): g_ffxBtlFloatingDigits = 0. From FFX_Btl_Init"),
    # The floating numbers, the draw half
    0x780CC0: ("FFX_BtlHud_PrepareNumbers", "(): from FFX_BtlMenu_DrawRoot (0x89B36D), drawn steps only. In any battle phase but 0, FFX_BtlHud_ProjectUnitAnchors then FFX_BtlHud_QueueFloatingNumbers"),
    0x79F380: ("FFX_BtlHud_ProjectUnitAnchors", "(): each existing unit's screen anchors on the 512 x 416 grid through sub_6428F0: the marker (+0xDF9 bone or the centre) into +0xF3C and +0xF40, -512 behind the eye, the overhead point (+0x442 bone or FFX_Btl_GetUnitCameraPoint, y less +0x4E8) into +0xF44 and +0xF48, and that clamped to 27..485 and 34..391 into +0xF4C and +0xF50. A draw side write into the unit, read only by FFX_BtlHud_QueueFloatingNumbers"),
    0x79F660: ("FFX_BtlHud_QueueFloatingNumbers", "(): outside phases 31..33 the Doom counters first (exists, on field, Doom, shown, shade alpha above 0): +0xF6C advances by doomStart / (count + 1) / 8 a draw, the digits at the marker 19 up, coloured from its sine. Then each record whose delay is 0 at the overhead anchor (or the clamped one for a negative style), moved by g_ffxBtlPopupBounce[age - 1 - index] for colour 0, up by age / 2 for 1, by age for 2, faded over its last five steps, pushed with FFX_BtlHud_PushPopup. Draws no RNG"),
    0x899A40: ("FFX_BtlHud_PushPopup", "(x, y, kind, glyph, r, g, b, a): one 16 byte entry onto g_ffxBtlPopupQueue, g_ffxBtlPopupQueueCount + 1. -1 at 63"),
    0x8FB840: ("FFX_BtlHud_DrawPopupDigit", "one damage digit from battle.dds (atlas 400), 34 x 45 HD scaled, u (digit % 5 * 49 + 444) / 1024, v (65 * (digit / 5) + 254) / 1024. From FFX_BtlMenu_DrawDamagePopups"),
    0x8FBCC0: ("FFX_BtlHud_DrawPopupLabel", "a popup label from icon.dds (atlas 600), the rect per label byte (':' ';' '<' '='). From FFX_BtlMenu_DrawDamagePopups"),
    # The party panel and the gauges
    0x8920D0: ("FFX_BtlMenu_StepPartyPanel", "(): every step from FFX_BtlMenu_Step (0x89AE2F, after RefreshTargetUnits). The panel's mode from the opener's command row byte 34 (bit 0 clear: 3 and a reset, else (b >> 1) & 3), FFX_BtlMenu_BuildMenuList(0, 18) for the front line (writes g_ffxBtlMenuListProperty215, step visible), a reset when the units change, then each row's HP and MP roll over 10 steps and its gauge 6 a step toward gauge * g_ffxBtlMenuGaugeFull / max. The rows at g_ffxBtlMenuPanelRows are read only by the draw"),
    0x898010: ("FFX_BtlMenu_ResetPartyPanel", "(): the three rows of g_ffxBtlMenuPanelRows rebuilt from list 18 with nothing rolling, the shown values the live ones, the status timer 20. Divides by the overdrive max with no zero check (0x89809E)"),
    0x891220: ("FFX_BtlMenu_StepEnemyGauges", "(): from FFX_BtlMenu_Step (0x89AE34). For each target enemy that FFX_BtlUi_ProjectUnitToScreen puts on screen, the gauge target gauge * g_ffxBtlMenuGaugeFull / max (0 for a max of 0) and the shown value 6 a step toward it. Step thread state that only the draw reads"),
    0x8960E0: ("FFX_BtlMenu_DrawOverdriveGauge", "the overdrive gauge bar. When full, a pulse from battle_kuang (870, 260)-(1332, 277) with alpha sin(3 t) * 32 + 96 off the wall clock, then the frame fill 0x80000000, the back 0x401A1A1A and the bar (w - 2) * value / full in 0x8000D4FF to 0x80008AFF, 0x80004AFF full. From FFX_BtlMenu_DrawPartyWindow and FFX_BtlMenu_DrawEnemyGauges"),
    0x8933C0: ("FFX_BtlMenu_DrawEnemyGauges", "(): from FFX_BtlMenu_DrawRoot (0x89B407), the target enemies' gauges FFX_BtlMenu_StepEnemyGauges keeps, through FFX_BtlMenu_DrawOverdriveGauge"),
    0x89D9B0: ("FFX_BtlMenu_DrawAbilityBanner", "(slot): when dword_133F510[4 * slot] is set, the ability name banner: a battle_kuang strip (0, 544)-(1600, 599) at (145, 96), the meswin tab at (145, 78), the name centred at y 101, scale 0.78. From FFX_BtlMenu_DrawRoot twice"),
    0x892590: ("FFX_BtlMenu_DrawOwnerMarker", "(unit): the spinning triangle over the menu's owner, three points turned by g_ffxBtlMenuCursorSpinAngle about the unit's point (sub_7950B0), projected by sub_6428F0 and drawn by sub_901190. DrawRoot skips it under Auto-Battle (0x89B446)"),
    # The text helpers
    0x905930: ("FFX_Text_DrawStringScaledOpaque", "(s, x, y, colour, scale, align): FFX_Text_DrawStringScaled with alpha 128"),
    0x8FAE40: ("FFX_Text_DrawGlyphRing", "the decoration FFX_Text_DrawStringScaled draws on layer 0 under each glyph: eight copies from the shadow page at 2 HD pixels in each direction, the offsets divided by FFX_Ui_GetOutlineScale. FFX_Text_DrawString draws a single drop shadow instead"),
    0x8B9140: ("FFX_Text_FormatNumberGlyphs", "(value, out, width): sprintf %d, or %<width>d for a width above 0, then the digits kept as glyph bytes, the padding spaces turned to a glyph per language and a sign dropped"),
    0x905190: ("FFX_Text_MeasureNumber", "(width, value, out, scale): the number as FFX_Text_FormatNumberGlyphs prints it, *out the sum of FFX_Text_GlyphAdvanceNoSnap per glyph less one FFX_Text_GlyphSpacing"),
    0x9057C0: ("FFX_Text_DrawNumberScaled", "(width, value, x, y, colour, scale, align): FFX_Text_FormatNumberGlyphs then FFX_Text_DrawStringScaledOpaque"),
    0x905890: ("FFX_Text_DrawNumberRight", "(value, xRight, y, colour, scale, ...): the number right aligned at xRight. Its digit count is the width, FFX_Text_MeasureNumber, then FFX_Text_DrawNumberScaled at xRight less the measure. The party panel's HP and MP"),
    0x8EC2F0: ("FFX_Text_DrawAsciiStringRgba", "(s, x, y, colour, r, g, b, a, scale, spacing): an ASCII string turned to glyph bytes per language (digits, letters, space, '-', '/', ':'), then drawn with FFX_Text_DrawGlyphShadow and FFX_Text_DrawGlyphQuad, 9 units higher than given"),
    0x901660: ("FFX_Text_DrawAsciiString", "(s, x, y, colour, scale, spacing): FFX_Text_DrawAsciiStringRgba with 0x80808080. FFX_BtlMenu_DrawPartyWindow calls it at 0x896C3D and 0x896F8C"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x893290: ("FFX_BtlMenu_DrawDamagePopups", "was maybe_FFX_BtlMenu_DrawDamagePopups. Confirmed: it draws the g_ffxBtlPopupQueue entries FFX_BtlHud_QueueFloatingNumbers pushed, a digit with FFX_BtlHud_DrawPopupDigit and a label with FFX_BtlHud_DrawPopupLabel, after the trophy latches"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x11360B8: ("g_ffxBtlFloatingDigits", "a pointer to the floating number records, a heap block of 128 x 20 bytes: style, colour, unit, index, glyph, delay, age, life, x, y, w, h, next (a pointer). Freed by FFX_Btl_FreeFloatingNumberPool"),
    0x11360BC: ("g_ffxBtlFloatingDigitHead", "a pointer to the first record of the list, 0 for none"),
    0x11360C0: ("g_ffxBtlFloatingDigitTail", "a pointer to the link the next record is stored through: the head after a reset, else the last record's next (+16)"),
    0x133F108: ("g_ffxBtlPopupQueueCount", "entries in g_ffxBtlPopupQueue, at most 63"),
    0x133F110: ("g_ffxBtlPopupQueue", "63 entries of 16 bytes (x, y, kind, glyph, r, g, b, a) FFX_BtlHud_PushPopup fills on a drawn step"),
    0x133D782: ("g_ffxBtlMenuPanelMode", "the party panel's mode, 0 HP, 1 MP, 2 status names, 3 the default with gauges. FFX_BtlMenu_StepPartyPanel sets it from the opener's command row byte 34"),
    0x133F7AC: ("g_ffxBtlMenuPanelUnits", "3 words, the units of the panel's rows from list 18, 255 for none"),
    0x133F7D8: ("g_ffxBtlMenuPanelRows", "3 rows of 144 bytes: HP target +0x0C, shown +0x10, step +0x14, count +0x18, colour +0x19, the same for MP at +0x24, gauge target +0x34 and shown +0x38, status timer +0x3C. Written only by FFX_BtlMenu_ResetPartyPanel and FFX_BtlMenu_StepPartyPanel"),
    0x25D0AA4: ("g_ffxBtlMenuGaugeFull", "the gauge's full value, battle_script.bin's dum9 width (FFX_MenuLayout_LoadCommon at 0x8AEE0A)"),
    0xC424B0: ("g_ffxBtlPopupBounce", "32 entries of (dx, dy, alpha), the damage number's bounce by age"),
    0xC424A0: ("g_ffxBtlPopupLabelKind3", "the one byte label string of a kind 3 number, ';'"),
    0xC424A4: ("g_ffxBtlPopupLabelKind4", "the one byte label string of a kind 4 number, '<'"),
    0xC424A8: ("g_ffxBtlPopupLabelKind1", "the one byte label string of a kind 1 number, ':'"),
    0xC424AC: ("g_ffxBtlPopupLabelKind5", "the one byte label string of a kind 5 number, '='"),
    0xB5F890: ("g_ffxFontGlyphColours", "8 colour slots of two RGBA words, R << 24 | G << 16 | B << 8 | A, slot n the bottom vertices [2n] and the top ones [2n + 1]. FFX_Font_ResolvePageTexture's glyph pass. Slot 0 grey to white, 2 yellow, 3 red"),
    0xB5F8D0: ("g_ffxFontShadowColours", "the shadow page's 8 slots, the same layout. Every shadow and decoration pass, and a glyph colour nibble of 8 or more"),
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
