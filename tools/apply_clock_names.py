"""
Apply the names from the 2026-10-28 clock census pass (reversing/THREADS_AND_CLOCKS.md rows R5, A22,
A23 and T13, Project Zanarkand PROGRESS 1.1.10) to the FFX.exe IDB.

Three clusters. The hair and cloth sway wind that row R5's rand() % 1000 scales, plus the job helper
that showed the four JobWorkers share one queue. The engine heap's deferred decommit cache and its
1 Hz scavenger thread, which was the unidentified thread T13. The touch region layer behind rows
A22 and A23, which Phyre's touch device feeds and which no shipped code ever enables.

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

    python apply_clock_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The sway wind, row R5
    0x824E60: ("FFX_Mtx_TransformDir", "out[i] = v.x*M[i] + v.y*M[4+i] + v.z*M[8+i] for i 0..3, a matrix times a direction with no translation, returns the input pointer. Reads no state. FFX_Ch_StepSecondaryPhysics uses it to rotate the wind gust (rand() % 1000 at 0x82395B) into a part's frame. THREADS_AND_CLOCKS.md R5"),
    0x824DD0: ("FFX_Ch_GetGlobalWind", "(strength*, yaw*, pitch*), copies g_ffxWindStrength, g_ffxWindYaw, g_ffxWindPitch out, each pointer null safe. ATEL Sg_055"),
    0x824E10: ("FFX_Ch_ResetGlobalWind", "strength 0.01, yaw 0, pitch 0. From FFX_Ch_EventjumpInit, which maybe_FFX_Map_SetupChars calls, so a map's character setup resets the sway wind"),
    0x824E30: ("FFX_Ch_SetGlobalWind", "(strength, yaw, pitch). ATEL Sg_054 SetWind, SG_DebugWin_PhysicsProc and sub_84CF60. A CHR with m_flags2 0x2 uses its own m_windYaw, m_windPitch, m_windStrength instead"),
    0x8450D0: ("FFX_Vec_DirFromYawPitchDeg", "(out, yaw, pitch), degrees: x = sin(yaw)cos(pitch), y = sin(pitch), z = cos(yaw)cos(pitch), w = 1. The sway wind direction in FFX_Ch_StepSecondaryPhysics"),
    0x82DBC0: ("FFX_Ch_CopySwayVertsToClone", "copies the sway result in CHRPART+0x08 into the cloned cloth vertex buffers of m_clonedClassChar (CHR+0x834), with face normals where the part asks. Same JobWorker job as FFX_Ch_StepSecondaryPhysics, skipped on catch-up render steps. Presentation"),
    0x6DBF50: ("JobSchedule__waitQueueDrained", "(queue): when fewer than 8 slots are free, takes all 8 counts of the free slot semaphore and gives them back, so it returns once every job on that queue has finished. Every caller passes 0, and all four JobWorkers pop queue 0 (0x6DBB35 push 0). THREADS_AND_CLOCKS.md T4"),
    # The engine heap's deferred decommit cache, thread T13
    0x946F50: ("FFX_VmCache_Init", "builds the 0x1C78 byte page cache at 0x193CD50: 256 free span buckets of 28 bytes, pending decommit list +7256, critical section +7180, tick +7276, thread +7280, wake event +7284, g_ffxVmCacheScavengerRun = 1. THREADS_AND_CLOCKS.md T13"),
    0x947590: ("FFX_VmCache_Commit", "under the cache lock, FFX_VmCache_ReclaimOrCommit, and on failure a forced FFX_VmCache_Scavenge and one retry. Behind FFX_Heap_CommitRange"),
    0x947690: ("FFX_VmCache_QueueDecommit", "does not decommit: queues (base, size) on the pending list with expiry tick + 1, so the scavenger releases it one to two seconds later. Behind FFX_Heap_DecommitRange"),
    0x9478E0: ("FFX_VmCache_Scavenge", "(this, force): under the lock, FFX_Vm_Decommit every pending range and FFX_Vm_Release every free span whose expiry the tick has reached, or all of them when forced"),
    0x947630: ("FFX_VmCache_StartScavenger", "starts FFX_VmCache_ScavengerThread through FFX_VmCache_CreateThread and stores the handle at +7280. From the lazy heap init sub_9428B0, which the CRT static initialiser sub_AF1290 reaches"),
    0x947820: ("FFX_VmCache_StopScavenger", "(this, flush): without flush, TerminateThread and wait. With flush, clears g_ffxVmCacheScavengerRun under the lock, sets the wake event, waits for the thread, then a forced FFX_VmCache_Scavenge"),
    0x947E30: ("FFX_VmCache_ScavengerThread", "the 1 Hz thread T13: while g_ffxVmCacheScavengerRun, wait the wake event up to 1000 ms, FFX_VmCache_Scavenge(cache, 0), tick += 1. The only effect the step could see is whether a recommitted heap range comes back stale or zeroed, which follows the wall clock. THREADS_AND_CLOCKS.md T13"),
    0x948C10: ("FFX_VmCache_CreateThread", "CreateThread(0, 0, FFX_VmCache_ThreadProc, fn, 0, 0). Its only live caller is FFX_VmCache_StartScavenger"),
    0x948C30: ("FFX_VmCache_ThreadProc", "the thread start routine, calls the fn it was handed"),
    0x9483D0: ("FFX_VmCache_ReclaimOrCommit", "commits [base, base + size): parts still on the pending decommit list are taken back without FFX_Vm_Commit, so they keep their old bytes, the gaps get FFX_Vm_Commit and come back zero"),
    # The touch region layer, rows A22 and A23
    0x6897D0: ("FFX_TouchRegions_Create", "allocates the 0x160 byte singleton g_ffxTouchRegions. From FFX_Input__ctor"),
    0x689830: ("FFX_TouchRegions_Destroy", "frees the arrays and the singleton. FFX_Input__dtor and the exit destructor sub_B066E0"),
    0x6898C0: ("FFX_TouchRegions_Get", "returns g_ffxTouchRegions. Called only by FFX_Input__ctor and FFX_Input__updateFromFFXApp"),
    0x68A660: ("FFX_TouchRegions_Init", "sizes 8 slots of 0x150 (+0x150 count, +0x154 array) and 2 groups of 0x1C8 (+0x158 count, +0x15C array) and resets them, every group's enable byte +1 is 0. From FFX_Input__ctor"),
    0x68A3A0: ("FFX_TouchRegions_Update", "per frame from FFX_Input__updateFromFFXApp: FFX_TouchRegions_ClearEdges, then every event of Phyre__PInput__findDeviceByType(3, 0) (3 is PE_INPUT_DEVICE_TOUCH in the SDK enum) to FFX_TouchRegions_HitTest"),
    0x68AB30: ("FFX_TouchRegions_ClearEdges", "per enabled group, clears the per region edge bytes, resets the state of released regions and zeroes their touch down time"),
    0x68B470: ("FFX_TouchRegions_HitTest", "matches one touch event against the 8 rectangles of every enabled group. Kind 1 inside a rectangle stores the id, the start and +0x10 = FFX_Time_AppElapsedSeconds (row A22). A group with enable byte +1 == 0 is skipped (0x68B4A6) and nothing live sets it, so in retail this does nothing. THREADS_AND_CLOCKS.md A22"),
    0x68A420: ("FFX_TouchRegions_ResetGroup", "zeroes one 0x1C8 byte group, the enable byte included, and sets each region id to 0xFF"),
    0x6898D0: ("FFX_TouchRegions_InitGroups", "zeroes n groups of 0x1C8 bytes"),
    0x689620: ("FFX_TouchRegions_FreeArrays", "frees the slot and group arrays and zeroes their counts"),
    0x68A8A0: ("FFX_TouchRegions_AddGroup_dead", "unreferenced (no xref, no pointer, no rel32 call): takes the first group with enable byte 0, stores the owner at +0x1C4 and the kind at +0, sets the enable byte. One of the only two stores that would enable the layer. THREADS_AND_CLOCKS.md A22"),
    0x68A910: ("FFX_TouchRegions_AddSlot_dead", "unreferenced: the same for the 0x150 byte slot array, the other store of an enable byte"),
    0x68B1D0: ("FFX_TouchRegions_HitTestGroup_dead", "unreferenced one group copy of FFX_TouchRegions_HitTest, the same stamp at +0x10 (0x68B268, row A23). THREADS_AND_CLOCKS.md A23"),
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
    0x12FBC44: ("g_ffxWindStrength", "the sway wind strength, times CHRDATA+0x3C and rand() % 1000 per sway pass (0x82395B, on a JobWorker). 0.01 after FFX_Ch_ResetGlobalWind. THREADS_AND_CLOCKS.md R5"),
    0x12FBC48: ("g_ffxWindYaw", "the sway wind yaw in degrees, 0 after FFX_Ch_ResetGlobalWind"),
    0x12FBC4C: ("g_ffxWindPitch", "the sway wind pitch in degrees, 0 after FFX_Ch_ResetGlobalWind"),
    0xC613B0: ("g_ffxVmCache", "points at the engine heap's page cache object at 0x193CD50 (FFX_VmCache_Init). THREADS_AND_CLOCKS.md T13"),
    0xC613AC: ("g_ffxVmCacheScavengerRun", "1 while FFX_VmCache_ScavengerThread should keep running, cleared by FFX_VmCache_StopScavenger"),
    0xCCC850: ("g_ffxTouchRegions", "the touch region singleton, 0x160 bytes (FFX_TouchRegions_Create). No group is ever enabled in retail. THREADS_AND_CLOCKS.md A22"),
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
