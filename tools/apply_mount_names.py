"""
Apply the VBF mount slot names from reversing/VBF_FORMAT.md ("The mount slots") to the FFX.exe IDB.

The VbfManager is the 68 byte heap object at g_ffxVbfManager 0xCC9C48. Its five VbfArchive*
mount slots sit at +16, +20, +24, +28 and +32. Censusing every writer of those five dwords named
the manager's teardown chain, the archive object's ctor and dtor, three orphan C++ special members
that nothing references, and the read request helpers the scan had to walk through.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all (0x61B1A0, 0x61B730 and 0x61BA10
are code with no function and no label). A name ending _ORPHAN has no reference anywhere in the image, proved
by a raw byte scan for rel32 branches and for the address as a dword.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_mount_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # manager lifecycle, the only writers of the five slots besides the ctor and mountArchive
    0x679590: ("FFX_ShutdownFileSystem", "pair of FFX_InitFileSystem. FFX_Lang_DestroyManager, FFX_VbfManager__destroySingleton, nullsub_235. Only caller FFXApplication__exitApplication 0x42F63D, so it runs once at process exit"),
    0x61B690: ("FFX_VbfManager__destroySingleton", "if g_ffxVbfManager: FFX_VbfManager__unmountAll, destroy the request pool (+12 block array, CloseHandle +4), FFX_Free the 68 bytes, g_ffxVbfManager = 0. Only caller FFX_ShutdownFileSystem"),
    0x61B620: ("FFX_VbfManager__unmountAll", "the manager dtor body. Frees the root path at +64, then for each of the 5 slots at +16..+32 runs FFX_VbfArchive__dtor, FFX_Free and stores 0 (0x61B666), then CryptReleaseContext(+0) and DeleteCriticalSection(+36). Retail reaches it only from FFX_VbfManager__destroySingleton at exit"),
    0x61AD70: ("FFX_VbfManager__construct_ORPHAN", "out of line full constructor: zeroes the request pool members +4..+12 and calls FFX_VbfManager__ctor. createSingleton inlines the same code. No reference anywhere in the image"),
    0x61AFA0: ("FFX_VbfManager__dtor_ORPHAN", "complete destructor: FFX_VbfManager__unmountAll then the request pool dtor. No reference anywhere in the image"),
    0x61B1D0: ("FFX_VbfManager__deletingDtor_ORPHAN", "scalar deleting destructor: FFX_VbfManager__unmountAll, the request pool dtor, FFX_Free(this) when (flags & 1). No reference anywhere in the image"),
    0x61BA10: ("FFX_VbfManager__releaseRequest_ORPHAN", "thiscall (manager, block): under the CS at +36, drops the block's refcount (+0x24) and on zero resets it and releases one semaphore permit (+4). The same code is inlined at the tail of FFX_VbfManager__readStream. No function, no reference"),
    0x61B730: ("FFX_VbfManager__acquireRequest_ORPHAN", "thiscall (manager): FFX_VbfReqPool__acquireBlock(INFINITE) on manager+4, then bumps the block's refcount at +0x24. The head of FFX_VbfReqPool__beginRequest on its own. No function, no reference"),
    0x61B7E0: ("FFX_VbfManager__buildReadRequest", "thiscall on the manager, called only by FFX_VbfManager__readStream. Walks chunkStoredSize[] from the stream's entry to the 64 KiB chunk holding the read offset, takes a request block with FFX_VbfReqPool__beginRequest (pool = manager+4) and fills it. Writes only the block, never the manager"),
    0x61AF40: ("FFX_VbfReqPool__dtor", "thiscall (pool = manager+4): destroys the 96 byte block array at pool+8 and CloseHandle(pool+0). Also the C++ unwind action of every manager constructor and destructor"),
    0x61C270: ("FFX_VbfReqBlock__reset", "clears a 96 byte request block and frees its scratch buffer, called when the block's refcount drops to zero"),
    # the archive object a slot points at
    0x61D460: ("FFX_VbfArchive__ctor", "96 byte VbfArchive: +0 header block 0, +4 Md5Map head (self linked) with a PMapPair pool at +28, +56/+60/+64 0, +92 0, CriticalSection at +68. Only caller FFX_VbfManager__mountArchive 0x61C1BE"),
    0x61D670: ("FFX_VbfArchive__dtor", "DeleteCriticalSection(+68), FFX_VbfArchive__freeBuffers, then the Md5Map at +4 (sub_61D5C0). Called by FFX_VbfManager__mountArchive on a failed open and by FFX_VbfManager__unmountAll"),
    0x61D910: ("FFX_VbfArchive__freeBuffers", "frees and zeroes the .vbf path buffer (+64), the resident header (+0), the chunk flag bytes (+60) and the 15 stream array (+92, FFX_VbfStream__dtor on each). Only caller FFX_VbfArchive__dtor"),
    0x61B1A0: ("FFX_VbfArchive__deletingDtor_ORPHAN", "scalar deleting destructor of the 96 byte VbfArchive. No function, no reference anywhere in the image"),
    # what a failed mount does next
    0x42F4D0: ("FFX_App_SendCloseToMainWindow", "SendMessageW(g_phyreMainHwnd, WM_CLOSE, 0, 0). FFX_InitFileSystem calls it after the mount failure MessageBoxW, and boot then carries on with every mount slot null"),
    0x6DB040: ("FFX_Lang_DestroyManager", "if dword_CDED58 (the sLanguageManager singleton FFX_Lang_GetManager returns): its dtor sub_6DAF30, FFX_Free, dword_CDED58 = 0. Called from FFX_ShutdownFileSystem"),
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
