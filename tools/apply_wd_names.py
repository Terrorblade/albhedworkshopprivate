"""
Apply the names from the 2026-10-28 wave dictionary pass (reversing/WAVE_DICTIONARIES.md, Project
Zanarkand PROGRESS 8.7) to the FFX.exe IDB.

The PS2 sound IOP path that used to open the .wd banks, which now ends in SG_Dev stubs, the two index
reloaders behind the constant kind 44 and 45 gate, and the battle sound section queue that still hands
the embedded WD chunk headers to FMOD. The unreferenced block at 0x8879D0 is left alone, it has no
function to name.

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

    python apply_wd_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x887450: ("FFX_Snd_LoadWdChunk", "skips a chunk of 0x30000 bytes or more, otherwise waits on the voice queue bits and hands the chunk to FFX_Se_LoadProjectFromWdChunk. [V]"),
    0x886F30: ("FFX_Snd_LoadSeSepChunk", "FFX_Se_LoadSeSepProjects, then the same voice busy bits as FFX_Snd_LoadWdChunk. [V]"),
    0x887A90: ("FFX_Snd_SetSoundRoot", "thunk to FFX_SndIop_SendSoundRoot, called with the PS2 sound root paths. [V]"),
    0x81EAB0: ("FFX_SndIop_SendSoundRoot", "copies the root path into off_C49458, then FFX_SndIop_CallRpc(2). Dead on PC, the RPC is a stub. [V]"),
    0x81CBD0: ("FFX_SndIop_CallRpc", "the EE to IOP sound RPC, ends in SG_Dev_RemoteRead_STUB_ret0 and retries while it returns below 0. [V]"),
    0x81D3F0: ("FFX_SndIop_SendWaveIndex", "a 20480 byte transfer then RPC 4, fed the kind 44 wave_wd.sec row. [V] body, [I] meaning"),
    0x81D340: ("FFX_SndIop_SendMusicIndex", "a 2048 byte transfer then RPC 5, fed the kind 45 music.sec row. [V] body, [I] meaning"),
    0x81EFD0: ("FFX_SndIop_Init_ORPHAN", "binds RPC 4096 and 4097 through SG_Dev_SifBindRpc_STUB_ret0. Its only caller 0x8879D9 sits in a block nothing references. [V]"),
    0x88E440: ("FFX_Snd_ReloadWaveIndex", "the kind 44 path to FFX_SndIop_SendWaveIndex, skipped by the constant byte at 0xB5EC4F (1, .rdata). Hex-Rays shows an empty body because of that gate. [V]"),
    0x88E3A0: ("FFX_Snd_ReloadMusicIndex", "the kind 45 twin of FFX_Snd_ReloadWaveIndex, skipped by the same constant gate. [V]"),
    0x88D1D0: ("FFX_Asset_ReadWholeSync", "loader slot 4 kind, slot 5 size, rounds to 2048, allocates, slot 0 read, slot 2 wait, returns the buffer. [V]"),
    0x7A01B0: ("FFX_Btl_QueueSoundSections", "walks a battle file's sound section table and queues each subsection through FFX_Btl_QueueSoundSection. [V]"),
    0x7A06B0: ("FFX_Btl_QueueSoundSection", "pushes onto the 16 slot g_ffxBtlSoundQueue, servicing it first when full. [V]"),
    0x7A0540: ("FFX_Btl_ServiceSoundQueue", "from FFX_Btl_MainStep 0x790CFD. Section 0 goes to FFX_Snd_LoadSeSepChunk, the rest to FFX_Snd_LoadWdChunk. [V]"),
    0x7FFFA0: ("maybe_FFX_SpuStream_LoadEffectSound", "SeSep, then FFX_Snd_LoadWdChunk(*(req + 12), 0) for a magic effect request. [I]"),
    0xA44740: ("SG_Dev_SifBindRpc_STUB_ret0", "xor eax, eax / retn. The bind role is inferred from FFX_SndIop_Init_ORPHAN. [V] body, [I] role"),
    0xA44770: ("SG_Dev_SifDma_STUB_retm1", "or eax, -1 / retn. [V] body, [I] role"),
    0xA44790: ("SG_Dev_SifSetDma_STUB_ret1", "mov eax, 1 / retn. [V] body, [I] role"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x81CF90: ("FFX_Se_LoadProjectFromWdChunk", "was maybe_FFX_Se_LoadProjectFromWdChunk. Checks the 0x4457 'WD' magic and passes the s16 at +2 to FFX_FmodCmd_LoadSeProject. The only read of a WD bank on PC. [V]"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x11360C8: ("g_ffxBtlSoundQueue", "16 slots of 12 bytes (chunk, size, actor, section), head dword_1136188, count dword_113618C. [V]"),
    0xB5EC4F: ("maybe_g_ffxHostFileModeConst", "1 in .rdata, 17 reads and no writes. Skips every kind 44 and 45 reader and gates parts of FFX_Btl_MainStep and core:465. The name guesses at intent. [V] value, [I] name"),
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
