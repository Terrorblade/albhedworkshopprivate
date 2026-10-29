"""
Apply the memory arena names from reversing/MEMORY_ARENAS.md to the FFX.exe IDB.

Covers the engine heap (the locked coalescing pools under FFX_MemAlloc, its page layer and its
globals), Phyre's allocator entry points (which are thin wrappers over that heap), the dead SG heap
from the PS2 mem.c with the block mover FFX_Ch_MoveChrMemory used to drive, the FFX_Free tracking
table, the dlmalloc mspace behind the cloth clone pool, and a few thin per subsystem wrappers.

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

    python apply_memory_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The engine heap, locked layer. One CRITICAL_SECTION at *0xCE901C guards everything.
    0x6FB9B0: ("FFX_Heap_Create", "Creates the engine heap on first use: malloc(0x30) for the CRITICAL_SECTION plus the 24 byte descriptor, then asks for a 0x30000000 span and steps down by 0x1000000 until a 16 byte probe allocation succeeds. Span to g_ffxHeapSpan. Reached lazily from every locked entry point while g_ffxHeapLock is null, so the first allocation of the process builds it. MEMORY_ARENAS.md 2.1"),
    0x6FB770: ("FFX_MemAllocAlignedLocked", "(size, align) from the engine heap: allocates size + align, rounds the pointer up, stores (ptr - raw) in the byte at ptr - 1. The 0xCD fill starts at the RAW pointer for size bytes, so the last (ptr - raw) bytes of the payload keep whatever the heap held before. MEMORY_ARENAS.md 2.1"),
    0x6FB820: ("FFX_MemCallocLocked", "(count, size) zeroed, from the engine heap. Its only caller is orphan code at 0x687000. MEMORY_ARENAS.md 2.1"),
    0x6FBB00: ("FFX_MemReallocLocked", "(ptr, size): allocates the new block, copies min(old payload, size), frees the old one. size 0 frees and returns 0. MEMORY_ARENAS.md 2.1"),
    0x6FB5D0: ("FFX_Heap_FreeOnHeapLocked", "thiscall free on an explicit lock plus descriptor, the same body as FFX_MemFreeLocked without the global. One caller, sub_945DE0. MEMORY_ARENAS.md 2.1"),
    0x686FF0: ("FFX_MemAllocAligned", "thunk to FFX_MemAllocAlignedLocked (size, align). Used for the SG heap block, the cloth clone pool base and about 30 aligned buffers. MEMORY_ARENAS.md 2.1"),
    0x687060: ("FFX_MemRealloc", "thunk to FFX_MemReallocLocked. MEMORY_ARENAS.md 2.1"),
    # The engine heap core, the pools and the page layer under them
    0x942A40: ("FFX_Heap_InitDescriptor", "(desc, span, minList, span - 0x100000): span page rounded at +0, the minimum free list size (0 here) at +4, +8, first pool +12 = 0, +16 = 16. MEMORY_ARENAS.md 2.1"),
    0x942B60: ("FFX_Heap_AllocBlock", "carves ((size + 15) & ~15) + 16 bytes out of the free block FFX_Heap_FindFreeBlock returns, commits the pages it touches, tags the header 2 (in use) and relinks the remainder. Returns 0 when no pool can satisfy it, and every caller then memsets through 0. MEMORY_ARENAS.md 2.1"),
    0x942DC0: ("FFX_Heap_CreatePool", "reserves one span, commits the first 8 KB and the last page, builds the 255 bucket heads (4080 bytes), the guard words, the two 16 byte sentinels and one free block of span - 4128. MEMORY_ARENAS.md 2.1"),
    0x943130: ("FFX_Heap_FreeBlock", "frees a block and coalesces with both neighbours, reading the previous block's tag at +12 as a free list node pointer when it is not 2. Decommits whole pages inside the merged block. The Sg_PcRead stamp crash lands under this. MEMORY_ARENAS.md 2.1"),
    0x943380: ("FFX_Heap_FindFreeBlock", "size class bucket search over each pool in turn (pool + 4080 is the next pool), creating a new pool of the same span when the last one has no fit. MEMORY_ARENAS.md 2.1"),
    0x943480: ("FFX_Heap_BlockSize", "payload size of a block, header +4 minus 16, or -1 when the pointer is in no pool. MEMORY_ARENAS.md 2.1"),
    0x9435A0: ("FFX_Heap_UnlinkFreeNode", "prev->next = next, next->prev = prev on a free list node. The faulting load of every Sg_PcRead heap crash is here. MEMORY_ARENAS.md 2.1"),
    0x947570: ("FFX_Heap_CommitRange", "commits a page range for the heap through FFX_Vm_Commit. MEMORY_ARENAS.md 2.1"),
    0x947670: ("FFX_Heap_DecommitRange", "decommits a page range for the heap through FFX_Vm_Decommit. MEMORY_ARENAS.md 2.1"),
    0x9438B0: ("FFX_Vm_Commit", "VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE), adds to g_ffxVmCommittedBytes. MEMORY_ARENAS.md 2.1"),
    0x9439A0: ("FFX_Vm_Reserve", "VirtualAlloc(0, size, MEM_RESERVE, PAGE_NOACCESS), adds to g_ffxVmReservedBytes. MEMORY_ARENAS.md 2.1"),
    0x9438E0: ("FFX_Vm_Decommit", "VirtualFree(addr, size, MEM_DECOMMIT), subtracts from g_ffxVmCommittedBytes, reports a failure through the callback at 0x193CD4C. MEMORY_ARENAS.md 2.1"),
    0x943940: ("FFX_Vm_Release", "VirtualFree(addr, 0, MEM_RELEASE), subtracts from g_ffxVmReservedBytes. MEMORY_ARENAS.md 2.1"),
    # Phyre's allocator, compiled as wrappers over the engine heap
    0x42FB40: ("PhyreMalloc", "PhyreMalloc(size): FFX_MemAlloc(size + 20), result rounded up to 16, the raw pointer stored in the aligned dword below it. 113 sites. MEMORY_ARENAS.md 3"),
    0x42FB80: ("PhyreMallocAligned", "PhyreMalloc(size, align): FFX_MemAlloc(size + align + 4), rounded to align, raw pointer in the aligned dword below. 1093 sites, the main Phyre allocation path. MEMORY_ARENAS.md 3"),
    0x42FAE0: ("PhyreMallocCount", "(count, size): FFX_MemAlloc(count * size + 20), 16 aligned. No zeroing. Reached only through two pointer tables. MEMORY_ARENAS.md 3"),
    0x42FBD0: ("PhyreRealloc", "(ptr, size): FFX_MemRealloc on the raw pointer with size + 20, then re-aligns to 16. Reached only through two pointer tables. MEMORY_ARENAS.md 3"),
    0x42FB20: ("PhyreFree", "PhyreFree(ptr): null safe, reads the raw pointer from the aligned dword below ptr and FFX_MemFree's it. 4314 sites. MEMORY_ARENAS.md 3"),
    # The FFX_Free tracking table, live but never filled
    0x630870: ("FFX_MemTracker_Create", "FFX_MemAlloc(0xAC) BacktraceOutputWin32 with a mutex at +168, into g_ffxMemTracker. Called from FFX_MemReport_CreateSingleton, the first step of FFX_GraphicInitialize, so it exists in retail. MEMORY_ARENAS.md 2.3"),
    0x630630: ("FFX_MemTracker_Record", "adds a 0x10C byte record for an allocation under the tracker mutex. Its three callers are all orphan code (the file and line operator new variants at 0x630320, 0x6304D0 and 0x6309B0), so the table stays empty in retail. MEMORY_ARENAS.md 2.3"),
    0x630A60: ("FFX_MemTracker_Forget", "called by FFX_Free on every free: takes the tracker mutex, looks the pointer up, frees the record when found. Nothing is ever recorded, so it always misses, at the cost of a mutex round trip per free. MEMORY_ARENAS.md 2.3"),
    0x62FE50: ("FFX_MemTracker_FindRecord", "thiscall tree lookup keyed by block address, used by FFX_MemTracker_Forget. MEMORY_ARENAS.md 2.3"),
    0x6DC870: ("FFX_MemReport_Destroy", "PhyreFree's g_ffxMemReport, deletes g_ffxMemTracker and clears it. From FFX__graphicFinalize. MEMORY_ARENAS.md 2.3"),
    # The SG heap (PS2 mem.c). Initialised at boot, never allocated from in this build.
    0x83C200: ("FFX_SgMem_Init", "mem.c init from FFX_ParseCommandLineAndInitDebug: g_ffxSgHeapBlock = FFX_MemAllocAligned(0x1100000, 16), one 17 MB SG heap over it, move cursor at its head. Nothing allocates from it in this build, the SG allocator at 0x83BDF0 is orphan code. MEMORY_ARENAS.md 2.2"),
    0x83C260: ("FFX_SgMem_InitHeap", "(id, start, end): fills descriptor id of g_ffxSgHeapTable (24 bytes each) and writes one free MCB covering start..end - 16 plus a closing MCB of type 15. MEMORY_ARENAS.md 2.2"),
    0x83C350: ("FFX_SgMem_MoveBlockDown", "(slot, &moved): moves the SG block *slot points at into the first free MCB below it that fits, from g_ffxSgMoveCursor, rewrites *slot and returns the distance. The mover FFX_Ch_MoveChrMemory calls. Reads SG MCB headers, so applied to an engine heap block it would misread the 0xABCDEF12 magic as a next pointer. Unreachable in retail. MEMORY_ARENAS.md 4"),
    0x83BDB0: ("FFX_SgMem_MarkFree", "clears an MCB to free (type 0) keeping its heap id bits, marks the heap dirty and lowers its first free hint. MEMORY_ARENAS.md 2.2"),
    0x83BAF0: ("FFX_SgMem_LinkFreeTail", "writes a free MCB after a moved block and links it between the block and its old successor. MEMORY_ARENAS.md 2.2"),
    0x83C5B0: ("FFX_SgMem_CoalesceAt", "merges a free MCB with its free neighbours. MEMORY_ARENAS.md 2.2"),
    0x83C6A0: ("FFX_SgMem_CoalesceHeap", "merges every run of free MCBs in one SG heap and clears its dirty byte. MEMORY_ARENAS.md 2.2"),
    0x83C5A0: ("FFX_SgMem_CoalesceHeap0", "FFX_SgMem_CoalesceHeap on SG heap 0. MEMORY_ARENAS.md 2.2"),
    0x83C630: ("FFX_SgMem_DumpHeap", "prints every MCB of one SG heap that is in use or larger than 63 bytes. Debug output only. MEMORY_ARENAS.md 2.2"),
    0x83BBC0: ("FFX_SgMem_DumpBlock", "prints one MCB. Debug output only. MEMORY_ARENAS.md 2.2"),
    0x83BB50: ("FFX_SgMem_DumpHeapById", "dumps SG heap 0, 1 or 2 then the chr cache. Debug only. MEMORY_ARENAS.md 2.2"),
    0x83B960: ("FFX_SgMem_CheckMcb", "range checks one MCB and its links against its heap, returns 0 or a negative code. MEMORY_ARENAS.md 2.2"),
    0x83C0B0: ("FFX_SgMem_BlockChecksum", "16 bit sum of an MCB payload, compared by SgMemCheckHeap for blocks with bit 0x4000000. MEMORY_ARENAS.md 2.2"),
    0x83BFF0: ("FFX_SgMem_LargestFreeBlock", "(mode): mode 1 coalesces heap 0 first, mode 2 runs FFX_Ch_MoveChrMemoryUntilDone and evicts the chr cache first, then returns the largest free payload in SG heap 0. Only caller FFX_Oef2_Op162 sub-op 0x5000. On PC heap 0 never changes, so mode 0 returns 0x10FFFE0 and modes 1 and 2 are never reached. MEMORY_ARENAS.md 4"),
    0x83C530: ("FFX_SgMem_SetMoveCursor", "g_ffxSgMoveCursor = p - 16, or heap 0's first MCB when p is 0. MEMORY_ARENAS.md 4"),
    0x83C560: ("FFX_SgMem_SetStampKey", "g_ffxSgStampKey = ((x - 8) >> 2) & 0x7FFFFF, the owner key the SG layer stamps into header bits 4..25. MEMORY_ARENAS.md 2.2"),
    0x83C510: ("FFX_SgMem_SetNoEvictOnce", "g_ffxSgNoEvictOnce = x. Read only by the orphan SG allocator, so it does nothing in this build. MEMORY_ARENAS.md 2.2"),
    0x83C480: ("FFX_SgMem_Realloc", "(heap, type, ptr, size) -> FFX_MemRealloc(ptr, size). The heap and type arguments are dropped on PC. MEMORY_ARENAS.md 2.2"),
    0x83F010: ("FFX_SgMem_ReallocHeap0", "FFX_SgMem_Realloc(0, 6, ptr, size). Its one caller passes size 0, which frees the block. MEMORY_ARENAS.md 4"),
    0x83F030: ("FFX_SgMem_ReallocHeap1", "FFX_SgMem_Realloc(1, 6, ptr, size). MEMORY_ARENAS.md 2.2"),
    0x83C070: ("FFX_SgMem_BlockDebugName", "debug name of an SG block from the 11 bit name index at header - 6, empty string unless g_ffxDebugMode. MEMORY_ARENAS.md 2.2"),
    0x83C0F0: ("FFX_SgMem_SumByType", "sums SG heap 0 block sizes into 9 buckets by MCB type and returns the heap size + 16. MEMORY_ARENAS.md 2.2"),
    # chr.c and mot users of the dead mover
    0x826230: ("FFX_Ch_InitMemoryMoveCursor", "from FFX_MainInit: FFX_MemAlloc(0x1000), FFX_SgMem_ReallocHeap0(p, 0) which frees it, then FFX_SgMem_SetMoveCursor(p). So the move cursor points at a freed engine heap header. Harmless only because the mover never runs. MEMORY_ARENAS.md 4"),
    0x827260: ("FFX_Ch_MoveChrMemoryUntilDone", "calls FFX_Ch_MoveChrMemory until it returns 0. Callers: FFX_SgMem_LargestFreeBlock mode 2 and the orphan SG allocator. Unreachable in retail. MEMORY_ARENAS.md 4"),
    0x837050: ("FFX_Mot_MoveBundleMemory", "moves the first resident motion bundle in g_ffxMotBundleTable that FFX_SgMem_MoveBlockDown can move and relocates it. Second phase of FFX_Ch_MoveChrMemory. Unreachable in retail. MEMORY_ARENAS.md 4"),
    # Thin per subsystem wrappers
    0x83F0A0: ("FFX_Sys_AllocAligned", "thunk to FFX_MemAllocAligned (size, align), the partner of FFX_Sys_FreeAligned. MEMORY_ARENAS.md 5"),
    0x83F100: ("FFX_Sys_Alloc", "thunk to FFX_MemAlloc. MEMORY_ARENAS.md 5"),
    0x90BBD0: ("FFX_Map_Alloc", "thunk to FFX_MemAlloc used by the map loaders. MEMORY_ARENAS.md 5"),
    0x90BC10: ("FFX_Map_AllocNoEvict", "FFX_SgMem_SetNoEvictOnce(1) then FFX_MemAlloc. The flag only mattered to the PS2 SG allocator. MEMORY_ARENAS.md 5"),
    0x9258A0: ("FFX_Rcbg_MemAlloc", "thunk to FFX_MemAlloc for the rcBg library, 44 sites. MEMORY_ARENAS.md 5"),
    0x9258B0: ("FFX_Rcbg_MemCalloc", "(count, size) FFX_MemAlloc then memset 0, for the rcBg library. MEMORY_ARENAS.md 5"),
    # dlmalloc 2.8 mspace, used only for the cloth clone pool
    0x6EBD30: ("FFX_Dl_CreateMspaceWithBase", "dlmalloc create_mspace_with_base(base, capacity, locked): refuses capacity <= 0x210, builds the mstate in the block, sets USE_LOCK when locked. One live caller, FFX_GraphicInitialize, for the 1 MB cloth clone pool. Shape matched to dlmalloc 2.8, not to a symbol. MEMORY_ARENAS.md 2.4"),
    0x6EA180: ("FFX_Dl_InitUserMstate", "dlmalloc init_user_mstate, places the mstate at the base of the given block. MEMORY_ARENAS.md 2.4"),
    0x6EC8D0: ("FFX_Dl_MspaceMemalignFilled", "(msp, align, size): dlmalloc memalign in the mspace, then fills the chunk payload with 0xCD. The clone pool's only allocation entry. MEMORY_ARENAS.md 2.4"),
    0x6EBEE0: ("FFX_Dl_MspaceFree", "(msp, ptr): dlmalloc mspace_free. Reached from FFX_Chr_FreeClonedClassCharacter and FFX_ChCache_FreeInstanceTables. MEMORY_ARENAS.md 2.4"),
    0x6EBDA0: ("FFX_Dl_DestroyMspace", "dlmalloc destroy_mspace, walks the segment list at msp + 456 and releases the mapped ones. Called only from orphan code at 0x63DD50. MEMORY_ARENAS.md 2.4"),
}

# ---------------------------------------------------------------------------------------------
# Functions whose current name is wrong and is replaced. The comment must start "was OLD."
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x6EA050: ("FFX_Dl_InitMparams", "was FFX_Arena_InitCookieFromTickCount. dlmalloc init_mparams: page size and granularity from GetSystemInfo, mmap threshold 256 KB, trim threshold 2 MB, default mflags 7, and magic = (GetTickCount() ^ 0x55555550) & ~15 | 8. It has nothing to do with the Monster Arena functions that share the old prefix. MEMORY_ARENAS.md 2.4"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xCE901C: ("g_ffxHeapLock", "LPCRITICAL_SECTION of the engine heap. The heap descriptor is the 24 bytes right after the section, so *g_ffxHeapLock + 24. Null until the first allocation. MEMORY_ARENAS.md 2.1"),
    0xCE9020: ("g_ffxHeapProbeBlock", "the 16 byte block FFX_Heap_Create allocates to prove a span works. Never freed. MEMORY_ARENAS.md 2.1"),
    0xCE9024: ("g_ffxHeapSpan", "the pool span FFX_Heap_Create settled on, 0x30000000 or less in 0x1000000 steps. Read at runtime, do not hardcode. MEMORY_ARENAS.md 2.1"),
    0x193CD44: ("g_ffxVmReservedBytes", "bytes the heap page layer holds reserved (FFX_Vm_Reserve minus FFX_Vm_Release). MEMORY_ARENAS.md 2.1"),
    0x193CD48: ("g_ffxVmCommittedBytes", "bytes the heap page layer holds committed (FFX_Vm_Commit minus FFX_Vm_Decommit). MEMORY_ARENAS.md 2.1"),
    0x1301960: ("g_ffxSgHeapBlock", "the 0x1100000 byte engine heap block the SG heap sits in, from FFX_SgMem_Init. MEMORY_ARENAS.md 2.2"),
    0x1301968: ("g_ffxSgHeapTable", "SG heap descriptors, 24 bytes each: +0 dirty byte, +1 enabled byte, +4 id, +8 size, +12 first MCB, +16 last MCB, +20 first free hint. Only heap 0 is ever initialised. MEMORY_ARENAS.md 2.2"),
    0x13019B0: ("g_ffxSgStampKey", "owner key stamped into SG header bits 4..25. Set by FFX_SgMem_SetStampKey. MEMORY_ARENAS.md 2.2"),
    0x13019B8: ("g_ffxSgMoveCursor", "the MCB FFX_SgMem_MoveBlockDown starts its search from. FFX_Ch_InitMemoryMoveCursor points it at a freed engine heap header. MEMORY_ARENAS.md 4"),
    0x1301958: ("g_ffxSgNoEvictOnce", "read only by the orphan SG allocator, cleared by it after one allocation. Dead in this build. MEMORY_ARENAS.md 2.2"),
    0xCCB15C: ("g_ffxMemTracker", "BacktraceOutputWin32 allocation tracker, created at boot. FFX_Free looks every pointer up in it under its mutex, but nothing is ever recorded. MEMORY_ARENAS.md 2.3"),
    0xCCB160: ("g_ffxMemCrtOwnedBlock", "a pointer FFX_Free hands to CRT free instead of the engine heap. Nothing writes it, so it is always 0 and FFX_Free(0) takes the free(0) branch. MEMORY_ARENAS.md 2.3"),
    0xC31E98: ("g_ffxMemTrackMinSize", "size threshold of the orphan tracked operator new variants, 0x1000 in the image, set to 1 by FFX_MemReport_CreateSingleton. MEMORY_ARENAS.md 2.3"),
    0xCCB458: ("g_ffxClothClonePoolBase", "the 1 MB block (FFX_MemAllocAligned(0x100000, 16)) the cloth clone mspace g_ffxClothClonePool lives in. MEMORY_ARENAS.md 2.4"),
    0xCE6EC4: ("g_ffxDlMparamsMagic", "was g_ffxArenaHeaderCookie. dlmalloc mparams.magic, (GetTickCount() ^ 0x55555550) & ~15 | 8, set once by FFX_Dl_InitMparams. Only the clone pool's chunk footers could carry it. MEMORY_ARENAS.md 2.4"),
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
