"""
Apply the names from reversing/MAGIC_DLL_CONTENTS.md to the FFX.exe IDB.

These are the exe side of the Yanagi effect engine (pppYanagi.c, the FFX-2 build names the file),
host slots 430..451 of g_ffxMagicHostApiTable 0xC64CE8. 40 magic DLLs are written over it: they
create a task through host 434, init a program through 436, and then run as chains of state
functions that push, set and wait through hosts 449, 450 and 451. The program side reads the
DLL's own data through FFX_Oef2_SetFromUnit and FFX_Oef2_GetSubTable, like the yonishi ports.

Hosts 449 and 450 carried DispList guesses. They push and set the task's current state function,
they do not touch a display list.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, or no name at all. A name starting maybe_ is a guess and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters. The script never saves the
database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python -I apply_dll_contents_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x925140: ("FFX_Yanagi_AllocTask", "sub_930030 allocates, sub_92FF20 inits, sub_930220 links the task into the list at owner +28"),
    0x925170: ("FFX_Yanagi_CreateTask", "host 434. (owner, state fn): FFX_Yanagi_AllocTask then FFX_YanagiTask_SetState(task, state fn). Every Yanagi family DLL starts its effect through it"),
    0x925210: ("FFX_Yanagi_InitProgram", "host 436. FFX_Oef2_SetFromUnit, then FFX_Oef2_GetSubTable(unit, 8, i) per sub program, then FFX_Vfx_RelocateParticleSet on the DLL's particle data"),
    0x925440: ("FFX_Yanagi_StepProgram", "host 437. Steps the program one frame and calls FFX_Btl_EffectAdvanceState when it ends"),
    0x9254F0: ("maybe_FFX_Yanagi_SetPppHeap", "host 438. Writes the current ppp heap pointer off_C3A4CC"),
    0x92EDE0: ("FFX_YanagiTask_PopState", "decrements the state stack depth at task +172 when it is above 0, so the state below becomes current again"),
    0x92EE00: ("maybe_FFX_YanagiTask_PopStateFlag2", "FFX_YanagiTask_PopState then task +0 |= 2. What bit 2 means is not known. Called by the wait tick when its counter runs out"),
    0x92EE60: ("FFX_YanagiTask_WaitFrames", "host 451. Pushes FFX_YanagiTask_WaitFramesTick with a frame counter at task +44 + 16 * index"),
    0x92EE90: ("FFX_YanagiTask_WaitFramesTick", "the state FFX_YanagiTask_WaitFrames pushes. Counts the frame counter down and at 0 calls maybe_FFX_YanagiTask_PopStateFlag2"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x92ECB0: ("FFX_YanagiTask_PushState", "was maybe_FFX_DispList_PushDwordChecked. Host 449. Pushes a state function onto the task's stack, slots at +12 + 4 * depth, depth at +172, refused at depth 7 so 8 slots. The Yanagi DLLs chain their phases through it. No display list is involved"),
    0x92EE20: ("FFX_YanagiTask_SetState", "was maybe_FFX_DispList_PushDword. Host 450. Writes the slot at the current depth, so it replaces the current state function without pushing. No display list is involved"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
}

PLACEHOLDER = re.compile(
    r"^(sub|nullsub|loc|j_sub|unk|dword|word|byte|qword|off|flt|dbl|stru|funcs)_[0-9A-Fa-f]+$")


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
