"""
Apply the menu draw path names from reversing/MENU_DRAW_PATH.md to the FFX.exe IDB.

The menu draw path runs from a module's draw slot (descriptor +0x04) and the menu objects' draw
slots (obj+0x10) through a few rect helpers into FFX_Sprite2D_Draw and FFX_DynDrawList_Append, then
out through the 2D list readers in FFX_Frame_RenderScene. Tracing it for Project Zanarkand 1.12.8
and 6.4.2 turned up the main menu's draw functions, the rect and fill helpers, the 2D list's
record and slot helpers, the menu shader loaders and the boot time ease table, all still
placeholders.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all. A name ending _UNUSED is code that
nothing in retail reaches.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_menu_draw_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the main menu, module 1
    0x8E0BA0: ("FFX_Menu_Module1Draw", "Module 1 (main menu) draw slot, descriptor 0xC5B2B0 +4, no code callers. Draws ffx_bg, the party plates (x = 145 + slide, y = 200 + 90i HD px, ease from FFX_Ease_ExpOut of g_ffxMenuPartySlide), HP, MP, level, cursors, gil, playtime and location. All layout is HD pixel constants through FFX_UiScaleX/Y. MENU_DRAW_PATH.md 11.1"),
    0x8E20F0: ("FFX_Menu_MainRowsDraw", "Draw slot of the main menu command list object (draw prio 7, set at 0x8E1F93). Skipped when the active mask has 0x80800. t = g_ffxMenuMainRowsOpen: divider at 1280, 194t by 2 x 805t, then every row via FFX_Menu_MainRowDraw, the cursor row last. MENU_DRAW_PATH.md 11.2"),
    0x8E2040: ("FFX_Menu_MainRowsStep", "Step slot of the command list object. While g_ffxMenuMainRowsOpening, g_ffxMenuMainRowsOpen += 341 to 4096, else -= 341 to 0. Each of 8 party slide words waits its delay word then adds 227 to 4096. The AND of all 8 goes to g_ffxMenuPartyRowsLanded. Menu step rate"),
    0x8E22A0: ("FFX_Menu_MainRowDraw", "(row, slide, -, -, t). One command row: x = 1304 + slide, y = ((194 + 75 row) t + 3 PS2 units (4096 - t)) / 4096, plate menu_new 476x60 at UV 1118, 74.7 row, alpha t / 32, label centred at scale 0.78, hand cursor at +115, +10 only on the cursor row when t == 4096"),
    0x8E2720: ("FFX_Menu_Module19Draw", "Module 19 (sphere grid) draw slot, descriptor 0xC5B2F4 +4. Black full frame fill, then when desc+0x1C is set sub_A54560, which can reach FFX_Menu_DrawAllPartsIgnoringCatchUp, so the PC sphere grid parts draw even on catch-up steps"),
    # rect helpers
    0x903EE0: ("FFX_Menu_DrawTexRect", "(texId, x, y, w, h, u0, v0, u1, v1, c0, c1), PS2 units. Clips through FFX_Text_ClipQuadToScissor, normalises UVs per atlas (-1 battle_kuang, -2 ffx_bg, -3 menu_new, -4 stonetexture /256, -6 summonbg, 0..398 meswin, 400..598 battle, 600..649 icon /1024 /512), then FFX_Sprite2D_Draw(rec, path, 1, 0, 0). c0 left, c1 right"),
    0x903BB0: ("FFX_Menu_DrawTexRectNeutral", "FFX_Menu_DrawTexRect with every colour channel 128 (neutral under MenuDefaultShader, which doubles)"),
    0x8F3DB0: ("FFX_Menu_DrawFillRect", "(x, y, w, h, c0, c1) ints, PS2 units. Untextured rect, FFX_Sprite2D_Draw(rec, NULL, 1, 0, 0), so NoTexture and MenuDefaultShaderNT. Skipped while a menu texture load is in flight. The menu dim quad is this with dim << 24"),
    0x8F4B20: ("FFX_Menu_DrawFillRectClipped", "Untextured rect through the menu scissor (sub_8E58E0). Used for the gil and playtime bands on the main menu"),
    0x8F5C60: ("FFX_Menu_DrawBackgroundFade", "(c0, c1). ffx_bg over 0,0..512,416, UV 0,0..1920,1024, colours forced to black keeping only the alpha bytes. FFX_Module_DrawAll passes col_menubg0/1 (0x25D1678..7F), 0x80000000 in game mode 0x400000"),
    0x8F5C10: ("FFX_Menu_DrawBackground", "ffx_bg over the full frame at neutral colour, FFX_Menu_DrawTexRectNeutral(-2, 0, 0, 512, 416, 0, 0, 1920, 1024)"),
    0x8E7D30: ("FFX_Menu_DrawScrollingGlow", "texture.dds (16001) rect whose UVs scroll by FFX_Time_AppElapsedSeconds times the given speeds. Wall clock, not step. The active party row shimmer on the main menu"),
    0x8C0670: ("FFX_Menu_DrawHandCursor", "(x, y, flag). Hand cursor, meswin id 200 UVs 0.0009765625, 0.76074219 .. 0.067382812, 0.8046875, bobbing by cos(AppElapsedSeconds * 8) * 3. Wall clock"),
    0x8C13D0: ("FFX_Menu_DrawHandCursorBlink", "The hand cursor variant drawn while a second selection is pending, on alternate halves of the menu step counter"),
    0x8AC8C0: ("FFX_Menu_GetAtlasPath", "Texture id to menu atlas path under /FFX_Data/GameData/PS3Data/menu or menu_us/D3D11. 15872 meswin, 16128 battle, 15808 icon, 11980 strtex are language dependent. 16000 battle_kuang, 16001 texture, 16002 ffx_bg, 16003 menu_new, 16004 stonetexture, 16006 summonbg, 11984 face_ply, 11988 face_smn, 11948 worldmap"),
    0x8F9230: ("FFX_Menu_SetClipRect", "(x, y, w, h) PS2 units. Writes the menu scissor flt_18716F8..flt_1871704 and sets dword_18716D0 = 1. FFX_MenuObj_DrawAllAtPrio sets the full frame before each priority"),
    0x8A98A0: ("FFX_MenuSys_GetDimLevel", "Gate for the menu dim quad in FFX_MenuSys_StepFrame. Level is g_ffxMenuDimLevel 0..128"),
    # render side
    0x650CB0: ("FFX_GfxCtx_Draw2DList", "Records the kind 0 2D records (Opaque then Transparent pass, only Transparent exists in the menu shaders) and kind 2 under the booster HUD with a scissor. Gated on gfx+69068 or the menu running and on g_debugInfoState+80. T0, inside FFXApplication__render"),
    0x653060: ("FFX_Frame_Draw2DOnlyMenuOpen", "The frame where a menu open is pending: clears to black and draws all 2D"),
    0x665010: ("FFX_GfxCtx_LoadMenuShaderNT", "Loads MenuDefaultShaderNT into gfx+0xFA80, the NoTexture 2D shader. VS doubles alpha only, PS rgb = v.rgb * v.a. From FFX_Boot_FirstAsyncLoad"),
    0x6650F0: ("FFX_GfxCtx_LoadMenuShader", "Loads MenuDefaultShader into gfx+0xFA84, the textured 2D shader. PS o = 2 * tex * vcol, rgb *= a, blend one invsrcalpha. From FFX_Boot_FirstAsyncLoad"),
    0x665580: ("FFX_GfxCtx_LoadFontShader", "Loads OptimizedFontShader into gfx+0xFA94, used for glyph records (+188 set). Alpha from the texture, colour from the vertex. From FFX_Boot_FirstAsyncLoad"),
    0x6653C0: ("FFX_GfxCtx_LoadMenuShaderNTZTest", "Loads the NTZTest menu shader into gfx+0xFA78. From FFX_Boot_FirstAsyncLoad"),
    # the 2D list
    0x67B970: ("FFX_DynDrawList_GetCount", "Record count of the set the render reads: +22592 when the toggle +23420 is set, else +11344"),
    0x67B990: ("FFX_DynDrawList_GetItem", "Record i of the set the render reads, through +16968 or +5616, gated on +23453"),
    0x67D920: ("FFX_DynDrawList_BindBatchGeometry", "Binds a pooled MenuDynamicGeometry to a record, picks the shader (glyph, RealFrameBuffer, NoTexture, default) and copies geom+40 (the pass kind) into record +180. geom+40 is not a sort depth"),
    0x67C210: ("FFX_DynDrawList_NewSingleRecord", "Takes a single quad record from the current set's pool (600 per set)"),
    0x67C170: ("FFX_DynDrawList_NewMultiRecord", "Takes a multi quad record (up to 12 quads) from the current set's pool (800 per set)"),
    0x67DC70: ("FFX_DynDrawList_PromoteToMulti", "Turns the open single record into a multi record when a second quad with the same texture arrives"),
    0x67B530: ("FFX_DynDrawList_FlushInterleaved", "Emits the open interleaved texture slots, layer 0 first, then layer 1, so text shadows land under glyphs"),
    0x67E4A0: ("FFX_DynDrawList_OpenSlot", "Opens interleaved slot n for a texture name (g_ffxDynDrawSlotTex / Count / Layer)"),
    0x67E310: ("FFX_DynDrawList_FindSlot", "Finds the open interleaved slot for a texture name"),
    0x67E4D0: ("FFX_DynDrawList_SetInterleavedImpl", "Writes list +23436, the interleaved flag. Called by FFX_DynDrawList_SetInterleaved"),
    0x67E4E0: ("FFX_DynDrawList_SetLayerImpl", "Writes list +23440, the interleaved layer. Called by FFX_DynDrawList_SetLayer"),
    0x67E3B0: ("FFX_DynDrawList_SetClipTransform_UNUSED", "Sets the append time clip at list +23456 with bounds +23480..+23492. Its only caller is at 0x6448C6 in code with no xrefs and no pointer to it, so the clip FFX_Sprite2D_ClipTest checks never turns on in retail"),
    0x644320: ("FFX_Sprite2D_ClipTest", "Append time clip of a sprite record against list +23480..+23492 when +23456 is set. Dead in practice, see FFX_DynDrawList_SetClipTransform_UNUSED"),
    0x6E69E0: ("FFX_ButtonPrompt_Substitute", "this = dword_CE6DC0. Called by FFX_Sprite2D_Draw with &g_ffxButtonPromptPendingA, &g_ffxButtonPromptPendingB, the record, &path, cornerMode. Swaps a prompt cell for the keyboard or pad prompt sheet and remaps the UVs. Non zero skips the draw"),
    # text and helpers
    0x905970: ("FFX_Text_DrawStringScaled", "The main menu's label call, FFX_Text_DrawString with a scale and an alignment byte. Command rows use scale 0.78"),
    0x88E750: ("FFX_Ease_ExpOut", "g_ffxEaseTableQ12[(4096 - a) / 64]. 4096 at a = 0 falling to 0 at a = 4096, geometric 0.9 per entry. Main menu party slide"),
    0x88E730: ("FFX_Ease_ExpIn", "g_ffxEaseTableQ12[a / 64], the mirror of FFX_Ease_ExpOut"),
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
    0x1871558: ("g_ffxMenuMainRowsOpen", "int Q12 0..4096, the main menu command list open amount. +341 per menu step while opening. Drives the divider height, row drop and row alpha"),
    0x1871554: ("g_ffxMenuMainRowsOpening", "Non zero while the main menu command list opens, zero while it closes. Read by FFX_Menu_MainRowsStep"),
    0x1871678: ("g_ffxMenuPartySlide", "8 x s16 Q12, each main menu party row's slide in amount. 0 hides the row, +227 per menu step to 4096"),
    0x1871668: ("g_ffxMenuPartySlideDelay", "8 x s16, steps each party row waits before its slide starts"),
    0x1871564: ("g_ffxMenuPartyRowsLanded", "s16, the AND of the 8 g_ffxMenuPartySlide words, 4096 once every row has landed"),
    0x184087C: ("g_ffxMenuDimLevel", "int 0..128, the main menu dim quad alpha. += g_ffxMenuDimStep per menu step in FFX_MenuSys_StepFrame"),
    0x1840880: ("g_ffxMenuDimStep", "int, added to g_ffxMenuDimLevel each menu step"),
    0x1840884: ("g_ffxMenuStepCounter", "u8, ++ per menu step in FFX_MenuSys_StepFrame, catch-up steps included. Blink and parity source for menu draws"),
    0xCCC81C: ("g_ffxDynDrawList", "The 2D draw list manager. Two record sets chosen by +23420, interleave flag +23436, layer +23440, dead clip +23456. Both FFX_Sprite2D_Draw and FFX_Glyph2D_Draw append here"),
    0xCCC7D4: ("g_ffxDynDrawSlotTex", "6 texture names of the open interleaved slots"),
    0xCCC7EC: ("g_ffxDynDrawSlotCount", "6 quad counts of the open interleaved slots"),
    0xCCC804: ("g_ffxDynDrawSlotLayer", "6 layers of the open interleaved slots (0 shadows, 1 glyphs)"),
    0xC5955C: ("g_ffxButtonPromptPendingA", "Button prompt substitution state passed by FFX_Sprite2D_Draw to FFX_ButtonPrompt_Substitute. Written from the draw path"),
    0xC59560: ("g_ffxButtonPromptPendingB", "Second word of the button prompt substitution state, see g_ffxButtonPromptPendingA"),
    0x23CBEE0: ("g_ffxEaseTableQ12", "65 x u16, built at boot in tklib__f88D710 (0x88D837): e[64] = 4096, e[k-1] = e[k] * 9 / 10, so 4096, 4096, 3686, 3317 .. 1, 0. Read by FFX_Ease_ExpOut and FFX_Ease_ExpIn"),
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
