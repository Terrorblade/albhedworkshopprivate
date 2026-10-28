"""
Apply the progression names from reversing/MINIGAMES_PROGRESSION.md to the FFX.exe IDB.

The Al Bhed primers, the sphere grid as a game system and the Celestial weapons. Mapping their
state and clocks turned up the key item giver every primer grant goes through, the primer import
path that reads other save files on the local disk, the glyph translator the message renderer
calls, the sphere grid's sphere use, node kind change and warp chain, the celestial weapon lookup
behind core:533, and three save block bytes whose meaning is now settled. Addresses are VAs.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all (the crest and sigil bytes at
0x112D6FC and 0x112D6FD have no label). A name starting maybe_ is a guess and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

Three existing comments are wrong and this script cannot fix them, because it only appends. The
function comments on FFX_AtelSys_Core_533_SetEquipAbilitiesFromTable_resi 0x85D170 and
FFX_Equip_SetAbilitySlotsFromTable 0x8C3170 call mcfr0100 actor 38 entry 9 the butterfly reward,
and it is the Celestial Mirror upgrade. The non-repeatable comment on 0x112D6C9 says the bit to
weapon mapping is not proven, and it now is. Edit those three by hand.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_progression_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # Al Bhed primers, MINIGAMES_PROGRESSION.md 2
    0x88E790: ("FFX_SaveData_GiveKeyItem", "(int id). FFX_SaveData_SetKeyItemFlag(id), then for id 0xA004..0xA01D FFX_SaveData_LearnAlBhedPrimer(id - 0xA004). Every primer grant goes through here: core:423, core:347, core:269, core:351, the import commit and dev Save_040. MINIGAMES_PROGRESSION.md 2.2"),
    0x86A040: ("FFX_AlBhed_CommitStagedPrimerGrants", "core:564 body. When g_ffxAlBhedPrimerGrantArmed is set, gives key item 0xA004+i for each bit i < 26 of g_ffxAlBhedPrimerGrantMask the primer mask lacks, then clears both. The mask came from save files on the LOCAL disk, a co-op hazard. MINIGAMES_PROGRESSION.md 2.4"),
    0x8729D0: ("FFX_AlBhed_GetImportedPrimerMaskForScript", "returns g_ffxAlBhedImportedPrimerMaskForScript, core:565. Local disk data. MINIGAMES_PROGRESSION.md 2.4"),
    0x906780: ("FFX_AlBhed_TranslateGlyph", "per glyph Al Bhed decision for a text object with +0x10 == 0x4000. Reads the primer mask unless g_ffxDebugAlBhedDictionaryOverride is -1 or 1. Returns 2 primer known (draw original), 1 substituted, 0 neither. Tables byte_187626D/E (language 0) and byte_18762A9/AA (others), stride 4. Presentation only, MINIGAMES_PROGRESSION.md 2.5"),
    0x906730: ("FFX_Debug_GetAlBhedDictionaryOverride", "returns g_ffxDebugAlBhedDictionaryOverride"),
    0x87F4A0: ("FFX_DebugPage_DrawAlBhedBooks", "debug page, Book%02d On/Off, one line per primer from FFX_SaveData_GetAlBhedPrimerMask"),
    # sphere grid, MINIGAMES_PROGRESSION.md 3
    0xA452D0: ("FFX_SphereGrid_StepUseSpherePage", "the sphere item page step. On confirm (pad 0x20) calls FFX_SphereGrid_UseSphereOnNode(menu+71100 char, menu+70396 node, item)"),
    0xA5BCA0: ("FFX_SphereGrid_UseSphereOnNode", "(char, node, item). Node record byte +34 & 3, then FFX_SphereGrid_CanUseSphereOnNode, then FFX_SaveData_AddItem(item, -1), then sphere.bin effect byte +13: 0 activates, else FFX_SphereGrid_ApplySphereEffect. The item is spent before the effect lands. MINIGAMES_PROGRESSION.md 3.3"),
    0xA493B0: ("FFX_SphereGrid_CanUseSphereOnNode", "(char, node, item). Item count non-zero, then FFX_SphereGrid_TestSphereOnNode with the sphere.bin row from item.bin +93"),
    0xA49440: ("FFX_SphereGrid_TestSphereOnNode", "(nodeRecord, ctx). Effect 0: char has not activated the node and panel +16 mask & sphere +10 mask and not a lock. Other effects: sub_A5D120"),
    0xA5CF30: ("FFX_SphereGrid_ApplySphereEffect", "(effect, node, char). 1..4 kind 1 Empty Node, 5/6/7/16..22 attribute kinds 25/35/36/5/9/13/17/21/29/33, 8..11 and 24 activate, 12..15 warp, 23 kind 1 and clear all seven bits. Which item is which effect is inferred. MINIGAMES_PROGRESSION.md 3.3"),
    0xA48740: ("FFX_SphereGrid_BeginNodeKindChange", "(newKind, node). Starts board effect 5 (Expert) or 3, stores kind at menu+71244 and node at menu+71246, installs FFX_SphereGrid_StepNodeKindChange"),
    0xA47D50: ("FFX_SphereGrid_StepNodeKindChange", "one menu frame. Counts menu+71248, at 20 MenuToSave, RecomputeDerived, then writes the new kind into the node record. Frame counted at 59.94 Hz"),
    0xA458A0: ("FFX_SphereGrid_ActivateNodeWithSe", "sound 0x80000050 then FFX_SphereGrid_ActivateNodeForChar(char, node). Effects 8..11 and 24"),
    0xA458C0: ("FFX_SphereGrid_ClearNodeBitForChar", "(char, node) menu node record +33 &= ~(1 << char). The Clear Sphere effect calls it for all seven"),
    0xA458F0: ("FFX_SphereGrid_UseWarpSphere", "(char, node) FFX_SphereGrid_BeginWarp then menu+71111 = 1. Effects 12..15"),
    0xA48A80: ("FFX_SphereGrid_BeginWarp", "(char, node). Target at menu+71220, char at menu+71224, installs FFX_SphereGrid_StepWarp"),
    0xA47F00: ("FFX_SphereGrid_StepWarp", "one menu frame. Fade out 40 frames, move 12 frames, write the char's current node at menu+69836+80*char, fade in 40 frames. Fades wait on word_1A8607E"),
    0xA56060: ("FFX_SphereGrid_OnQuitConfirm", "quit path. One branch returns to the page sub_A5A270 with a sound, the other commits: MenuToSave, RecomputeDerived, sub_8E27E0"),
    # Celestial weapons, MINIGAMES_PROGRESSION.md 4
    0x8C30E0: ("FFX_Equip_FindCelestialWeapon", "(kind). First entry of FFX_SaveData_ListEquipForChar(kind, -1, 0, &n) with flags & 4 and forChar == kind. Kinds >= 7 find nothing, so core:533 on them is a no-op"),
    0x7ABD10: ("FFX_SaveData_ListEquipForChar", "(char, a2, a3, int *outCount) lists equipment entries for a character. Free slots are listed too"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0xA56160: ("FFX_SphereGrid_ConfirmOrCancelMove", "was FFX_Menu_SphereGridActivateNode. It does not activate a node. a3 == 0 confirms a move and charges the move cost menu+71196 through FFX_SaveData_SpendSphereLevels, the only S.Lv spend. a3 == 1 cancels and restores the node from menu+71216. MINIGAMES_PROGRESSION.md 3.3"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x1325AC4: ("g_ffxAlBhedImportedPrimerMask", "primer mask read from other save files on the local disk by FFX_SaveFile_ImportPrimerFromSlot. core:605 writes it, FFX_AlBhed_GetImportedPrimerMask reads it. Not in the save block, differs per machine"),
    0x1325B8C: ("g_ffxAlBhedImportedPrimerMaskForScript", "the same import result, core:565 returns it. Local disk data"),
    0x1325B90: ("g_ffxAlBhedPrimerGrantArmed", "set by FFX_AlBhed_StagePrimerGrantMask, consumed by FFX_AlBhed_CommitStagedPrimerGrants"),
    0x1325B94: ("g_ffxAlBhedPrimerGrantMask", "the staged primer bits, see g_ffxAlBhedPrimerGrantArmed"),
    0x18763E0: ("g_ffxDebugAlBhedDictionaryOverride", "-1 no primer known, 1 all known, else the save mask. Only FFX_Debug_CycleAlBhedDictionary writes it. Text only"),
    0xC86D00: ("g_ffxCelestialAbilityRows", "core:533 ability rows. 8-byte rows of four u16, row 1 crest {No AP, Double OD}, row 2 sigil. Per-kind bases 0xC86D00..0xC86E10 in MINIGAMES_PROGRESSION.md 4.4"),
    0x112D6C9: ("g_ffxCelestialWeaponsFound", "was maybe_g_ffxSaveWeaponMasterBits. g_ffxSaveData+0xC39, proved: 0x01 Tidus 0x02 Yuna 0x04 Wakka 0x08 Auron 0x10 Kimahri 0x20 Lulu 0x40 Rikku, set after each weapon's core:347. MINIGAMES_PROGRESSION.md 4.2"),
    0x112D6FC: ("g_ffxCelestialCrestApplied", "g_ffxSaveData+0xC6C, script var3 in mcfr0100 a38, bit 1 << charId set when core:533(char, 1) applies the crest"),
    0x112D6FD: ("g_ffxCelestialSigilApplied", "g_ffxSaveData+0xC6D, script var4 in mcfr0100 a38, bit 1 << charId set when core:533(char, 2) applies the sigil"),
    0x112D70F: ("g_ffxJechtSphereCount", "was maybe_g_ffxSaveJechtSphereStage. g_ffxSaveData+0xC7F, Jecht Spheres found after the first, incremented once in each of nine packages, 9 = all ten. MINIGAMES_PROGRESSION.md 5"),
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
