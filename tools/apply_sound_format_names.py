"""
Apply the sound format names from reversing/SOUND_FORMATS.md to the FFX.exe IDB.

These are the functions and globals that turn a game sound id into an FMOD event, found while
reading the shipped .fev and .fsb files against FFX.exe (PROGRESS 1.9.1 and 1.9.2):

    SE      the shim loads SFX/<lang>/%04d.fev by number (command 48), the resident set from
            g_ffxSeResidentProjects, the rest from map and SeSep data. SfxProject reads
            <project>_common.txt and <project>_loop.txt, which turn a sequence id into event
            indices in the "nonloop" and "loop" groups
    BGM     g_ffxFmodBgmEventIndex turns a track into an event index in the "music" group
    voice   VoiceIDMapper turns an id into an event name, the first two characters pick the slot

Plus the orphan FMOD wrappers left by the 40 FMOD imports audit.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all (the orphan wrappers here have no
function and no label). A name starting maybe_ is a guess and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_sound_format_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.

The framework below the tables is copied from apply_sound_names.py, keep the two in step.
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
    # SE project loading on the step thread, every path ends in command 48
    0x887240: ("FFX_Se_LoadResidentProjects", "loads up to 16 int16 project numbers as resident (command 48 with resident 1), the list is the argument or g_ffxSeResidentProjects when NULL. Sets dword_132FF24 = 1"),
    0x81E3B0: ("maybe_FFX_Se_LoadProjectFromHeader", "a1+12 is a size in 1..0x10000, then command 48 with the int16 at a1+8, not resident"),
    0x81E600: ("FFX_Se_LoadSeSepProjects", "walks 'SeSep   ' records (dwords 0x65536553 0x20202070, size at +12, a multiple of 4), collects the u16 project at +17 into g_ffxSeSepProjectIds without duplicates (256 max, logs 'Too many wave datat'), then command 48 for each, not resident"),

    # SfxProject side tables, the two halves of the sequence id -> event lookup
    0x710370: ("SfxCommonTable__load", "reads <project>_common.txt, records of {u32 sequence, u32 event index} into a tree keyed by sequence. From SfxProject__loadCommonTable after getGroup(\"nonloop\")"),
    0x710290: ("SfxCommonTable__getEvent", "sequence -> the stored index -> EventGroup vtable +36 getEventByIndex(index, 0). Returns the FMOD::Event* or 0"),
    0x710C50: ("SfxLoopTable__load", "reads <project>_loop.txt, records of {u32 sequence, u32 n, u32 index[n]}. From SfxProject__loadLoopTable after getGroup(\"loop\")"),
    0x710AE0: ("SfxLoopTable__getEvents", "sequence -> n event indices, each through EventGroup vtable +36 getEventByIndex, appended to the vector at a3. Returns 0 when the sequence is unknown"),

    # orphan SfxEvent getters, no xrefs. this+8 is the {FMOD::Event*, bool valid} pair
    0x70EB40: ("maybe_SfxEvent__getNumInstances", "orphan. When this[0] >= 1, Event::getInfo into a 0x34 byte FMOD_EVENT_INFO and returns +0x28, which is numinstances in FMOD Ex 4.44"),
    0x70EB90: ("maybe_SfxEvent__getPan", "orphan. Event::getChannelGroup, ChannelGroup::getChannel(0), Channel::getPan. SfxEvent.cpp lines 0x11B and 0x11E"),
    0x70EC00: ("maybe_SfxEvent__getVolume", "orphan. Event::getVolume, SfxEvent.cpp line 0x10F"),

    # orphan FMOD wrappers
    0x7066A0: ("FfxFmod_CreateEventSystemOrphan", "orphan, FMOD_EventSystem_Create(a1). FfxFmod__init calls the import directly"),
    0x7066B0: ("FfxFmod_MemoryInitializeOrphan", "orphan, FMOD_Memory_Initialize with its 6 arguments passed through. FfxFmod__init calls the import directly"),
    0x7067F0: ("FfxFmod__getMemoryCurrentAlloc", "FMOD_Memory_GetStats(&current, &max, 0), returns current or 0 on failure. Reached only by the tail jump in maybe_FmodManager__getMemoryCurrentAlloc"),
    0x6FA260: ("maybe_FmodManager__getMemoryCurrentAlloc", "orphan. FfxFmod = this+8, 0 when NULL, else tail jumps to FfxFmod__getMemoryCurrentAlloc"),
    0x707A70: ("FfxFmod_GetMemoryCurrentAllocOrphan", "orphan, the same body as FfxFmod__getMemoryCurrentAlloc"),
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
    0xC54DB0: ("g_ffxSeResidentProjects", "16 int16 SE project numbers, 0xFFFF ends a slot. Ships as 0, 1, 2005, the default list of FFX_Se_LoadResidentProjects"),
    0x12FB3C0: ("g_ffxSeSepProjectIds", "256 dwords, the deduplicated project numbers FFX_Se_LoadSeSepProjects collects before sending command 48 for each"),
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
