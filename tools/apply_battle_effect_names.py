"""
Apply the battle effect names to the FFX.exe IDB (2026-11-01).

These come from item 4 of the battle port in Project Zanarkand (PROGRESS 13.13 and 15.1): the effect start,
the Oef2 object pool and lists, the link records and the battle's half of the effects (src/engine/fx/EffectUnit.cpp
and src/ffx/btl/BattleEffects.cpp). Each function was read in the decompile and the disassembly while porting
it. See reversing/EFFECT_OVERLAY.md, reversing/YONISHI_ENGINE.md and reversing/BATTLE_START_AND_END.md (the
2026-11-01 lines).

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

    python apply_battle_effect_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The battle's starts and ends
    0x7FC2E0: ("FFX_Oef2_StartBattleProgram", "(data, set, program, handle): with g_ffxSgDebugEnabled 0 it starts at once (FFX_Oef2_StartProgram) and returns 1. With it 1 and no bits in word_2321580 (the shipped boot) it starts only g_ffxBattleEffectUnit, a set in g_ffxWeaponEffectSets (FFX_Oef2_FindWeaponEffectSet) or one in dword_119FED0 (FFX_Oef2_FindShadowRegistrySet), and returns 1. A null or unknown set is -1. Bits 1, 2 and 4 swap in the debug sets dword_23215C0, dword_23215C8 and dword_23215D0. The start's own result is dropped. dword_C48D78 = handle"),
    0x79EB10: ("FFX_Btl_StartUnitEffect", "(unit, set, kind, program, a5): 0 for unit 0xFF. The serial first, +0xE22 for kind 1 and +0xE1E for any other (0x79EB4A), packed by FFX_Magic_PackEffectHandle, then FFX_Oef2_StartBattleProgram on the unit's +0x4C (a5 0 or g_ffxBtlDbgTestHitEff 0). The handle when that answers 1, else 0. A monster has no +0x4C, so its hit effects count the serial and start nothing. FFX_Btl_StartHitEffect calls it with kind 8"),
    0x795A60: ("FFX_Btl_IsDefeatSequenceOver", "(unit): +0x3F4 of a valid unit, else 0. Oef2 op 53 mode 0 waits on it, which is how et_battle's death programs wait for the unit's defeat sequence to end (sub_78CCA0 sets it) before they fade it"),
    0x79E6A0: ("FFX_Btl_LoadBattleEffectSet", "(): et_battle.bin, asset kind 13 row 7, into a 331760 byte buffer (FFX_Btl_InternalError eff_mem_over when larger), g_ffxEtBattleData = it, then FFX_Oef2_SetBattleEffectUnit. From FFX_MainInit 0x82098A and the battle teardown in FFX_Btl_MainStep 0x790E43"),
    0x79E7A0: ("FFX_Btl_BeginBattleEffects", "(): op_et_battle_effect_init (the effect unit's busy byte +0x3A = 0), then dword_112A95C = g_ffxEtBattleData. From FFX_Btl_BeginBattle's allocate step"),
    0x79E630: ("FFX_Btl_FreeBattleEffectSet", "(): with g_ffxEtBattleData set, op_et_battle_effect_free, FFX_MemFree and both g_ffxEtBattleData and dword_112A95C = 0"),
    0x784000: ("FFX_Btl_BindWeaponDataSections", "(unit): over the wep_data at +0x48, for a unit with +0x11 set. With +0x4C 0: +0x4C = the section the u32 at +8 names when the u32 at +0 is above 2 and that offset is not 0 (FFX_Btl_RegisterUnitEffectSet), else 0. Then the sound sections of the u32 at +4 when the count is above 1 (FFX_Btl_QueueSoundSections). From FFX_Btl_OnWeaponDataLoaded and FFX_Btl_LoadWeaponModel when +0x48 is set already"),
    0x783FC0: ("FFX_Btl_OnWeaponDataLoaded", "(load): the asset callback of FFX_Btl_LoadWeaponModel. FFX_Btl_BindWeaponDataSections on the load's unit, then for a load of mode 0 the next unit below 18 with +0xDC8 set whose FFX_Btl_LoadWeaponModel starts a load (sub phase 18, the cursor in the high byte of word_112A928), or sub phase 19 when none is left"),
    0x788B70: ("FFX_Btl_RegisterUnitEffectSet", "(data, unit): +0x35 = 1, then FFX_Oef2_RegisterWeaponEffectSet(data, unit)"),
    # The Oef2 sets, objects, lists and link records
    0x7FD7F0: ("FFX_Oef2_StartProgram", "(data, set, program, handle): data registered in the effect unit's 16 slot registry (+0x60), an object in list 0 (FFX_Oef2_AllocObject) with thread 0 at set + 4 * program + the s32 there, its link record (FFX_Oef2_GetLinkRecord(handle, slot, 1)), its unit from the record's first target, or the caster for 0xFF, and its id (program << 8) + 16, bumped by 16 while an object of lists 0 and 1 with the same slot has it. A handle with bits 0x70000000 or kind 3 sets +0xB3 = 127. Returns the id + (slot << 16)"),
    0x7FB240: ("FFX_Oef2_GetBusyCount", "(): byte +0x3A of effect unit 0, the count Oef2 op 82 cases 1 and 2 take up (to 250) and down. FFX_Btl_EndPhaseStep's drain waits until it is 0, so a battle's end waits on every death particle"),
    0x7FB250: ("FFX_Oef2_SetBattleEffectUnit", "(data): sub_7FDF70(effect unit 0, old) when there was one, g_ffxBattleEffectUnit = data, FFX_Oef2_SetFromUnit, then FFX_Oef2_StartProgram(data, 0, 0, -1), et_battle's resident program 0"),
    0x7FCB60: ("FFX_Oef2_RegisterWeaponEffectSet", "(data, unit): nothing when data is in g_ffxWeaponEffectSets already. Else the first free of its four slots takes it, with byte_119FEF8 and byte_119FEFC of the slot 0 and g_ffxWeaponEffectSetUnits = unit, then FFX_Oef2_SetFromUnit and FFX_Oef2_StartProgram(data, 0, 0, -1). Nothing when all four hold other sets"),
    0x7FE570: ("FFX_Oef2_FindWeaponEffectSet", "(data): its slot in g_ffxWeaponEffectSets, 0..3, else -1"),
    0x7FE540: ("FFX_Oef2_FindShadowRegistrySet", "(data): its slot in dword_119FED0, the 8 sets FFX_Ch_RegisterShadowRegistryEntry writes, else -1"),
    0x801700: ("FFX_Oef2_AllocObject", "(unit, list): -1 when the list has no free entry or no object of the pool (unit +0x20, 256 bytes each) has a null IP 0, both bounded by the capacity at +0x34 less 1. Else the first such object's index appended to the list (unit +0x10 + 4 * list) and the live count +0x38 up"),
    0x801200: ("FFX_Oef2_ClearObject", "(obj): all 256 bytes zero from the zero vector, then +0xC8 = 0x80808080"),
    0x800EB0: ("FFX_Oef2_InheritFromSpawner", "(unit, obj, from): obj's position (+0x30) and colour (+0x40) and draw colour (+0x90) from from, +0x1C its parent (from's index), and from's +0x1E slot, +0x14 record, +0x18 unit, +0xBA move and +0xB3 draw mask. Op 50's spawn"),
    0x8014C0: ("FFX_Oef2_FreeObject", "(unit, index): the index taken out of list 1, else list 0, else list 2, the four IPs cleared, its prims freed by +0xBB (3 a part through header +0x54, 5, 9, 10 and 17 dynamic geometry groups, the rest op_free) and the live count +0x38 down. Thread 0's op 0 end"),
    0x800D10: ("FFX_Oef2_UnlinkOverlay", "(unit, index): the index taken out of the overlay list (unit +0x1C)"),
    0x800D60: ("FFX_Oef2_LinkOverlay", "(unit, index): the index appended to the overlay list (unit +0x1C) unless it is there already, bounded by the capacity at +0x34 less 1. Op 77"),
    0x801060: ("FFX_Oef2_FindObjectById", "(unit, obj, id): the object of lists 1, 0 and 2 with the id (+0x1A) and obj's slot (+0x1E), else the pool's first object. A negative id is relative: -256 obj itself, -1 its parent (+0x1C), -n the n-th parent, 0xFExx the low nibble on the record root's id, others obj's id + 0x4000 + id. Ops 46 and 128"),
    0x7FEBA0: ("FFX_Oef2_GetLinkRecord", "(handle, slot, build): the g_ffxEffectLinkRecords entry already holding the handle, record 0 for -1, else a free one from 1, its targets built for a battle handle (FFX_Btl_BuildTargetOrder, or FFX_Btl_BuildHitTargetList(handle, &count, 0) for an ability of class 136..143, 0x7FEC53 pushes 0 so it never draws) and the caster from FFX_Btl_UnitIdPassthrough"),
    0x7FE920: ("FFX_Oef2_ReleaseLinkRecords", "(handle): nothing for -1. Else every g_ffxEffectLinkRecords entry with the handle back to free: handle -1, object 0, count and built 1, targets 0, caster 0 and slot 255. Op 127 mode 0 and FFX_Oef2_ClearListsAndAdvance"),
    0x7FE6C0: ("FFX_Oef2_ClearListsAndAdvance", "(record): every entry of the four lists of the unit in dword_12A40C4 set to -1, then the record (for a negative one the first with a nonzero handle and an object at or past that unit's pool) released (FFX_Oef2_ReleaseLinkRecords) and its handle handed to FFX_Btl_EffectAdvanceState. Reached from FFX_Oef2_Op001_KillObject"),
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
    0x11360A8: ("g_ffxEtBattleData", "et_battle.bin, the battle effect set, as FFX_Btl_LoadBattleEffectSet reads it. FFX_Btl_BeginBattleEffects copies it into dword_112A95C, the set every death effect and critical flash starts from"),
    0x119FEA0: ("g_ffxWeaponEffectSets", "4 slots, the units' own effect sets (a party unit's wep_data effect section, +0x4C) that FFX_Oef2_RegisterWeaponEffectSet holds. FFX_Oef2_StartBattleProgram starts a set only when it is here, the battle unit or in dword_119FED0"),
    0x119FE9C: ("g_ffxWeaponEffectSetUnits", "4 bytes, the unit of each g_ffxWeaponEffectSets slot (FFX_Oef2_RegisterWeaponEffectSet)"),
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
