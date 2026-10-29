# Memory arenas: every allocator in FFX.exe, who calls it, and what Phyre sits on

Written 2026-10-28 for Project Zanarkand items 1.3.2 (arena policy, every `FFX_Alloc` family
caller by arena, the compaction order of `FFX_Ch_MoveChrMemory`) and 1.3.3 (Phyre's allocator and
how FFX's sits on it). Read from a private copy of the FFX.exe IDB through idalib. Addresses are VAs
at the preferred base 0x400000. Names are the ones `tools/apply_memory_names.py` applies, the old
placeholder is given the first time a renamed function appears.

Tags: **[V]** read in the binary at the address given, **[M]** measured by a scan over the IDB,
**[I]** inferred. Only [V] and [M] are proof.

Companion docs: `../loader/workshop/include/ffx/addresses/Memory.h` and `EngineHeap.h` (the engine
heap's block header and walk, item 1.3.1), `CHR_POOL.md`, `CHR_CLUSTER.md` section 8 (the cloth
clone pool), `MAP_PARTICLES.md` and `YONISHI_ENGINE.md` (the ppp heap), `THREADS_AND_CLOCKS.md`
(the thread roots used in section 6), `BOOT_SEQUENCE.md`.

---

## 1. The answer in short

1. **There is one real heap.** Everything that is not a fixed pool or a CRT `malloc` ends in the
   engine heap behind `FFX_MemAllocLocked 0x6FB910` and `FFX_MemFreeLocked 0x6FB8B0`: a set of
   coalescing pools of up to 768 MB each, guarded by one `CRITICAL_SECTION`. [V]
2. **Phyre does not have a heap of its own in this build.** `PhyreMalloc`, `PhyreMallocAligned`,
   `PhyreRealloc` and `PhyreFree` (0x42FB40, 0x42FB80, 0x42FBD0, 0x42FB20, all `sub_` before this
   pass) are 16 byte over-allocating wrappers that call `FFX_MemAlloc` and `FFX_MemFree`. FFX's
   heap is underneath Phyre, not the other way round. [V 0x42FB40, 0x42FB80, 0x42FB20]
3. **`FFX_Alloc 0x630310`, `FFX_Malloc 0x6304C0` and `FFX_Free 0x630390` are the program's global
   `operator new`, `operator new[]` and `operator delete`.** The CRT's nothrow `new`
   (`??2@YAPAXIABUnothrow_t@std@@@Z`) calls `FFX_Alloc`, `type_info`'s deleting destructor and the
   std facet code call `FFX_Free`, and the `FFX_Malloc` sites carry MSVC's `new[]` overflow guard
   (`neg eax; or eax, ecx; push eax` at 0x57D544). So every `new` in the game, in Phyre, in Bullet's
   unaligned classes and in the std library lands in the engine heap. [V]
4. **The PS2 heap (the SG heap of `mem.c`) is dead.** `FFX_SgMem_Init 0x83C200` reserves a 17 MB
   block for it at boot, and nothing ever allocates from it: the SG allocator at 0x83BDF0 is orphan
   code with no function and no reference. [V, M]
5. **`FFX_Ch_MoveChrMemory 0x826FA0` never runs in retail.** Its only live path is
   `FFX_Oef2_Op162` sub-op 0x5000, which asks the dead SG heap for its largest free block first and
   leaves at 0x81700D because the answer is always 0x10FFFE0. The compaction order is written up in
   section 4 for completeness, but there is no compaction to reproduce and nothing reads block
   order. Item 3.5.2's retirement stands, for a stronger reason than the one it gives. [V]
6. **OOM in the engine heap is a crash, not a handled path.** `FFX_Heap_AllocBlock` returns 0 when no
   pool fits, and `FFX_MemAllocLocked` then `memset`s through it. Every `if (p == 0)` after an
   `FFX_Alloc` is dead. The handled failure paths all belong to fixed pools (section 2.7). [V]

---

## 2. The arenas

| arena | init site | backing memory | size or budget | lock | threads | free path |
|---|---|---|---|---|---|---|
| engine heap | `FFX_Heap_Create 0x6FB9B0` (was `sub_6FB9B0`), lazily from the first allocation | `VirtualAlloc` reserve per pool, pages committed on demand | span asked 0x30000000, stepped down by 0x1000000 until a probe allocation works, stored in `g_ffxHeapSpan 0xCE9024`. A new pool of the same span when the last is full | one `CRITICAL_SECTION` at `*g_ffxHeapLock 0xCE901C` | any | `FFX_MemFreeLocked`, coalescing, pages decommitted |
| SG heap (PS2 `mem.c`) | `FFX_SgMem_Init 0x83C200` from `FFX_ParseCommandLineAndInitDebug` | one engine heap block, `FFX_MemAllocAligned(0x1100000, 16)` | 17 MB, never used | none | boot only | none in this build |
| `FFX_Free` tracker | `FFX_MemTracker_Create 0x630870` from `FFX_MemReport_CreateSingleton` | engine heap | empty forever | a mutex at tracker +168 | any | not an arena, a lookup on every free |
| cloth clone pool | `FFX_Dl_CreateMspaceWithBase 0x6EBD30` from `FFX_GraphicInitialize` (step 78) | one engine heap block, `FFX_MemAllocAligned(0x100000, 16)` in `g_ffxClothClonePoolBase 0xCCB458` | 1 MB, dlmalloc mspace | mspace `USE_LOCK` | step thread | `FFX_Dl_MspaceFree 0x6EBEE0` |
| ppp heaps | `FFX_PppHeap_Init 0x72C480`, `FFX_Ppp_InitHeapForSet 0x7169D0` | static buffers, plus one engine heap block for the map fx extra heap | 256 KB map fx, 500 KB menu and sphere grid, 1 MB vfx, 512 KB magic resident, caller sized for Yanagi and DLLs | none | step thread | `FFX_PppHeap_Free 0x72C510`, coalescing |
| Oef2 object heap | inside each Oef2 object's work area | the object | per object | none | step thread | `op_free 0x7FF1D0` |
| fixed pools | per pool, section 2.7 | static arrays or one engine heap block | fixed counts | none | step thread, except the async load pools | slot flag |
| CRT heap | MSVCR110 | process heap | unbounded | CRT | any | `free` |

### 2.1 The engine heap

The block and pool layout is in `Memory.h` and `EngineHeap.h` and is not repeated here. What this
pass adds is the layering, the budget and the fills.

**Layers, bottom up.** [V for each body]

| layer | functions |
|---|---|
| page layer | `FFX_Vm_Reserve 0x9439A0` (`MEM_RESERVE`, `PAGE_NOACCESS`), `FFX_Vm_Commit 0x9438B0`, `FFX_Vm_Decommit 0x9438E0`, `FFX_Vm_Release 0x943940`. Running totals in `g_ffxVmReservedBytes 0x193CD44` and `g_ffxVmCommittedBytes 0x193CD48`. `FFX_Heap_CommitRange 0x947570` and `FFX_Heap_DecommitRange 0x947670` sit on top |
| pool core | `FFX_Heap_InitDescriptor 0x942A40`, `FFX_Heap_CreatePool 0x942DC0`, `FFX_Heap_FindFreeBlock 0x943380`, `FFX_Heap_AllocBlock 0x942B60`, `FFX_Heap_FreeBlock 0x943130`, `FFX_Heap_UnlinkFreeNode 0x9435A0`, `FFX_Heap_BlockSize 0x943480` |
| locked | `FFX_MemAllocLocked 0x6FB910`, `FFX_MemAllocAlignedLocked 0x6FB770`, `FFX_MemCallocLocked 0x6FB820`, `FFX_MemReallocLocked 0x6FBB00`, `FFX_MemFreeLocked 0x6FB8B0`, `FFX_MemFreeAlignedLocked 0x6FB6C0`, `FFX_Mem_HeapBlockSize 0x6FBA90`, `FFX_Heap_FreeOnHeapLocked 0x6FB5D0` |
| thunks | `FFX_MemAlloc 0x687020`, `FFX_MemFree 0x687010`, `FFX_MemAllocAligned 0x686FF0`, `j_FFX_MemFreeAlignedLocked 0x686FE0`, `FFX_MemRealloc 0x687060`, `FFX_Mem_BlockSize 0x687050` |
| C++ | `FFX_Alloc 0x630310` (`operator new`), `FFX_Malloc 0x6304C0` (`operator new[]`), `FFX_Free 0x630390` (`operator delete`) |
| Phyre | `PhyreMalloc 0x42FB40`, `PhyreMallocAligned 0x42FB80`, `PhyreMallocCount 0x42FAE0`, `PhyreRealloc 0x42FBD0`, `PhyreFree 0x42FB20` (section 3) |
| subsystem wrappers | `FFX_Sys_AllocAligned 0x83F0A0`, `FFX_Sys_FreeAligned 0x83F090`, `FFX_Sys_Alloc 0x83F100`, `FFX_Tk_AllocSectorRounded 0x88E650` (2 KB rounding), `FFX_Tk_Free 0x88D520` (then `SgMemCheckHeap` twice, which does nothing because the SG heap is never dirty), `FFX_Map_Alloc 0x90BBD0`, `FFX_Map_AllocAligned64 0x90BBE0`, `FFX_Map_AllocNoEvict 0x90BC10`, `j_j_FFX_MemFreeLocked_1 0x90BBC0`, `FFX_Rcbg_MemAlloc 0x9258A0`, `FFX_Rcbg_MemCalloc 0x9258B0`, `j_j_FFX_MemFreeLocked_2 0x9258E0`, `FFX_Ch_BlobBlockSize 0x83C050`, the FMOD callbacks `FfxFmod_MemAlloc 0x7066D0`, `FfxFmod_MemRealloc 0x706730`, `FfxFmod_MemFree 0x706710` |

**Creation.** `FFX_MemAllocLocked`, `FFX_MemAllocAlignedLocked`, `FFX_MemCallocLocked` and
`FFX_MemReallocLocked` each call `FFX_Heap_Create` when `g_ffxHeapLock` is 0 [V 0x6FB910,
0x6FB770, 0x6FB820, 0x6FBB00]. `FFX_Heap_Create` takes `malloc(0x30)` from the CRT for the
critical section and the descriptor, tries a 0x30000000 span, and on failure frees and retries
0x1000000 smaller until a 16 byte probe allocation (`g_ffxHeapProbeBlock 0xCE9020`) succeeds [V
0x6FB9B0]. The first allocation of the process therefore builds the heap, and that is a static
initializer, because the CRT dynamic initializers in 0xAE0000..0xB0C000 call `PhyreMallocAligned`
before `WinMain` [M, the census puts 294 Phyre layer sites there]. The heap is never destroyed.

**Budget.** One pool is one span. `FFX_Heap_FindFreeBlock` walks the pools through pool + 4080 and
calls `FFX_Heap_CreatePool` for another span when the last has no fit [V 0x943380]. In a 32 bit
process a second 768 MB reservation will normally fail [I], `FFX_Heap_FindFreeBlock` returns 0, and
the caller crashes in the `memset`. The project measured a 768 MB span at runtime (`Memory.h`).
`FFX_Heap_InitDescriptor` stores span - 0x100000 at descriptor +8, no reader found [V 0x942A40].

**Fills.** What a fresh block contains is what an uninitialized read sees, which is why this
matters for parity (section 7).

| entry | fill | where |
|---|---|---|
| `FFX_MemAllocLocked` | 0xCD over the requested size | `memset(p, 0xCD, size)` [V 0x6FB910] |
| `FFX_MemAllocAlignedLocked` | 0xCD over `size` bytes **from the raw block start**, not from the aligned pointer, so the last `aligned - raw` bytes (up to `align - 1`) keep stale heap contents | [V 0x6FB770] |
| `FFX_MemCallocLocked` | zero | [V 0x6FB820] |
| `FFX_MemReallocLocked` | none beyond the copy, the tail is whatever `FFX_Heap_AllocBlock` returned | [V 0x6FBB00] |
| `FFX_MemFreeLocked` | none | [V 0x6FB8B0] |
| `FFX_MemFreeAlignedLocked` | 0xFE over the whole block before freeing | [V 0x6FB6C0] |
| `PhyreMalloc`, `PhyreMallocAligned`, `PhyreMallocCount` | 0xCD, through `FFX_MemAlloc` over the padded size | [V] |
| `FfxFmod_MemAlloc` | 0xCD | [V 0x7066D0] |

**Alignment.** Every engine heap block is 16 aligned [V 0x942B60 rounds to 16 and the header is 16].
`FFX_Mem_SetNextAllocAlign 0x83C4A0` (55 call sites) writes `g_ffxMemNextAllocAlignMask 0x1301964`,
and the only reader of that mask is the orphan SG allocator at 0x83BEA8 [M, every xref listed]. So
every "align the next allocation to 64" request in the game is a no-op on PC, including
`FFX_Map_AllocAligned64` and `FFX_Tk_AllocSectorRounded`. The aligned entry points that do work are
`FFX_MemAllocAligned` and the Phyre wrappers.

**Thread rule.** Every path takes the one critical section, so the heap may be used from any thread,
and it is (section 6). [V]

### 2.2 The SG heap (PS2 `mem.c`), dead in this build

`FFX_SgMem_Init 0x83C200` is called from `FFX_ParseCommandLineAndInitDebug` at 0x822519 (boot, on a
JobWorker, `BOOT_SEQUENCE.md` step 86) and from orphan code at 0x82068A [V]. It takes
`FFX_MemAllocAligned(0x1100000, 16)` into `g_ffxSgHeapBlock 0x1301960`, and
`FFX_SgMem_InitHeap 0x83C260` writes one free MCB over all of it and a closing MCB of type 15 [V].

The MCB header is 16 bytes: +0 next, +4 prev, +8 a 16 bit checksum and a name index, +12 a tag word
with the type in bits 0..3 (0 free, 15 end), an owner key in bits 4..25, a checksum flag in bit 26
and the heap id in bits 27..28 [V `FFX_SgMem_InitHeap`, `SgMemCheckHeap 0x83B7B0`,
`FFX_SgMem_CheckMcb 0x83B960`]. The descriptor table is `g_ffxSgHeapTable 0x1301968`, 24 bytes per
heap, and only heap 0 is ever initialised [V].

**Nothing allocates from it.** The SG allocator is the code at 0x83BDF0..0x83BFEA, which reads the
alignment mask, `g_ffxSgNoEvictOnce` and `g_ffxSgStampKey`, retries through
`FFX_Ch_MoveChrMemoryUntilDone` and `FFX_ChCache_EvictOne` when full, and stamps the owner key. It
is not a function in the IDB and no code or data refers to it [M]. The SG heap's writers are
`FFX_SgMem_InitHeap`, the mover and its helpers (`FFX_SgMem_MoveBlockDown 0x83C350`,
`FFX_SgMem_MarkFree 0x83BDB0`, `FFX_SgMem_LinkFreeTail 0x83BAF0`, `FFX_SgMem_CoalesceAt 0x83C5B0`),
the coalescer `FFX_SgMem_CoalesceHeap 0x83C6A0` and `sub_83B720` from the dead
`FFX_ChCache_CompactBlobs_dead`. None of them is reachable once the mover is shown dead (section 4),
so heap 0 keeps its boot state for the life of the process. [V, M]

What survives of `mem.c` in live code:

- `FFX_Mem_SetNextAllocAlign`, `FFX_Ch_SetAllocWriteOnlyFlag 0x83C4D0`, `FFX_SgMem_SetNoEvictOnce
  0x83C510` and `sub_83C520` set globals that only the orphan allocator reads, or that nothing reads
  (`g_ffxChrAllocWriteOnlyFlag 0x13019B4` and `byte_13019B5` have no reader at all). [M]
- `SgMemCheckHeap 0x83B7B0` walks heap 0 only when its enabled byte is set, which it is, and finds the
  boot state every time. Called from `FFX_Tk_Free` and 25 other sites. [V]
- `Sg_PcRead_StampHeaderWord_HEAP_CORRUPTION 0x83C4E0` stamps the owner key into the SG tag word at
  `p - 4`. On PS2 that was this layout's +12. On PC `p` is an engine heap block and `p - 4` is the
  engine heap's tag, which is the known corruption (`Memory.h`, item 1.3.1). [V, the PS2 reading I]
- `FFX_SgMem_Realloc 0x83C480` keeps the `(heap, type, ptr, size)` signature and drops the first two.
  `FFX_SgMem_ReallocHeap0 0x83F010` has one caller and it passes size 0, so it frees (section 4). [V]

Naming note: item 1.3.1 calls the engine heap's 16 byte header "the PS2 heap header". It is the PC
engine heap's header (magic 0xABCDEF12, tag 2 in use). The PS2 heap is this section's MCB layout,
and it is dead.

### 2.3 The `FFX_Free` tracker

`FFX_MemReport_CreateSingleton 0x6DC800` is the first call in `FFX_GraphicInitialize` (0x6419DF) and
it calls `FFX_MemTracker_Create 0x630870`, which builds a `BacktraceOutputWin32` with a mutex into
`g_ffxMemTracker 0xCCB15C` and sets `g_ffxMemTrackMinSize 0xC31E98` to 1 [V]. From then on every
`FFX_Free` calls `FFX_MemTracker_Forget 0x630A60`: wait on the mutex, look the pointer up with
`FFX_MemTracker_FindRecord 0x62FE50`, release [V 0x630390, 0x630A60]. The only function that adds a
record, `FFX_MemTracker_Record 0x630630`, is called from three file and line `operator new`
variants at 0x630320, 0x6304D0 and 0x6309B0, all orphan [M]. So the table is always empty and every
free pays a mutex round trip for nothing.

`FFX_Free` also compares its argument with `g_ffxMemCrtOwnedBlock 0xCCB160` and hands a match to CRT
`free`. Nothing writes that global, so it is 0, and `FFX_Free(0)` takes the `free(0)` branch [M].

### 2.4 The cloth clone pool, a dlmalloc mspace

`FFX_GraphicInitialize` steps 77 and 78 (`BOOT_SEQUENCE.md`): `g_ffxClothClonePoolBase 0xCCB458 =
FFX_MemAllocAligned(0x100000, 16)`, then `g_ffxClothClonePool 0xCCB454 =
FFX_Dl_CreateMspaceWithBase(base, 0x100000, 1)` [V 0x641B5B, 0x641B6C].

The allocator is Doug Lea's dlmalloc 2.8 in mspace form [V for each body, the identification by
shape is I]: `FFX_Dl_InitMparams 0x6EA050` (was `FFX_Arena_InitCookieFromTickCount`) is
`init_mparams`, with the spin lock, `GetSystemInfo`, mmap threshold 0x40000, trim threshold 0x200000,
default mflags 7 and `magic = (GetTickCount() ^ 0x55555550) & ~15 | 8` into `g_ffxDlMparamsMagic
0xCE6EC4`. `FFX_Dl_CreateMspaceWithBase` refuses a capacity of 0x210 or less, builds the mstate
through `FFX_Dl_InitUserMstate 0x6EA180` and sets `USE_LOCK` when its third argument is nonzero, which
it is. The only allocation entry is `FFX_Dl_MspaceMemalignFilled 0x6EC8D0` (memalign then 0xCD over
the chunk), the free is `FFX_Dl_MspaceFree 0x6EBEE0`. `FFX_Dl_DestroyMspace 0x6EBDA0` is reached only
from orphan code at 0x63DD50 that would have rebuilt the pool [M].

Users: `FFX_Chr_CloneClassCharacterVertexData 0x63A810` (the 560 byte clone records, per mesh tables
and cloth objects, `CHR_CLUSTER.md` 8), `FFX_Chr_FreeClonedClassCharacter 0x63A160`,
`FFX_ChCache_FreeInstanceTables 0x640E70` [V]. With the default mflags, a request of 256 KB or more
would bypass the 1 MB block and go straight to `VirtualAlloc` [I, dlmalloc's rule, no such request
seen]. The tick count magic only reaches chunk footers, and nothing hashes this pool (row C1 of
`THREADS_AND_CLOCKS.md`).

### 2.5 The ppp heaps

A first fit allocator with a 16 byte block header `{next, prev, used}`, sizes rounded to 16, a roving
start pointer at heap +0, first and end blocks at +4 and +8, bytes in use at +12, no lock, no fill
[V `FFX_PppHeap_Alloc 0x72C3D0`, `FFX_PppHeap_Init 0x72C480`, `FFX_PppHeap_Free 0x72C510`].
`maybe_FFX_pppMemAlloc 0x716A30` tries the main heap, then the extra heap when one is set, and sets
`g_ffxPppAllocFailed` when both fail. `FFX_Ppp_HeapFreeBlock 0x716A90` picks the heap by address.
`FFX_Ppp_InitHeapForSet` also points `g_ffxPppCurrentHeap 0xC3A4CC` at the heap. [V]

| instance | heap struct | buffer | size | set up by |
|---|---|---|---|---|
| map placed effects | `g_ffxMapFxHeap 0xCEC22C` | static 0xCEC258..0xD2C258 | 0x40000 | `FFX_MapFx_LoadParticleSet` 0x72A829 [V] |
| map fx extra heap | `unk_CEC23C` | `g_ffxMapFxExtraHeapBlock 0xCEC224`, engine heap | header +1 x 8 KB | 0x72A85E [V] |
| menu and sphere grid | `dword_1A86034` | static `dword_16C1870` | 0x7D000 | `FFX_Menu_Build`, `FFX_SphereGrid_ActivateNodeForChar`, `_BeginNodeKindChange`, `_BeginWarp`, `_StepWarp` [V] |
| vfx system | `dword_10D4CFC` | static `unk_FD24F0` | 0x100000 | `sub_760610`, `sub_761D60` [V] |
| magic resident overlay | `dword_12F2070` | static `unk_2343520` | 0x80000 | `FFX_MagicResident_OvlInit` [V] |
| Yanagi programs | caller's | caller's | caller's | `FFX_Yanagi_InitProgram` [V] |
| magic DLLs | their own | their own | their own | host API slots 337 and 710 [V] |

### 2.6 The Oef2 object heap

`op_malloc 0x7FF780` and `op_free 0x7FF1D0` manage space inside an Oef2 object's work area: the
allocation records are 16 byte slots that grow down from the area's end pointer, and the space grows
up. An overflow prints "op_malloc error (heap area over)" and writes to address 0, a deliberate
crash [V 0x7FF780]. No lock. 22 alloc and 11 free sites, all Oef2 ops and effect host functions
(appendix B). Separately, `sub_7E6C30` gives the Oef2 layer one 16 KB engine heap scratch block at
`dword_119FE10` [V].

### 2.7 Fixed pools

These are where the game actually handles running out. Counts are [V] at the address given unless
the row cites another doc.

| pool | where | capacity | full means |
|---|---|---|---|
| CHR pool | `FFX_Ch_AllocChrArray 0x825670`, one engine heap block | `2 * count * 0x880`, count = max(36, actors + n + 8) (`CHR_POOL.md`) | `FFX_Ch_Allocate` writes through 0, callers must guarantee a slot |
| CHRDATA | `g_ffxChrDataTable 0x23C8D00`, static | 40 x 300 | `FFX_Ch_BlkAllocate 0x825760` falls off the end and writes through 0 |
| chr cache | `FFX_ChCache` (`CHR_RESIDENCY.md`) | 16 | eviction |
| menu objects | `FFX_MenuObj_Alloc 0x8AA1A0`, static 0x1840900 | 32 x 152 (0x1300 / 152) | returns 0 |
| prim build | `FFX_Vfx_PrimBuildAllocSlot 0x728660`, six `FFX_Malloc(0x2C6B8)` buckets | 6 x 342 slots of 532 bytes, cost cap 100000 per bucket | returns 0 |
| async load requests | `AsyncLoadManager` (`BOOT_SEQUENCE.md` 41) | 200 cluster, 150 task | `ASYNC_LOAD_REQUESTS.md` |
| job ring | `JobSchedule` (`THREADS_AND_CLOCKS.md` T4) | 8 | the poster waits |
| Phyre command ring | `Phyre_CommandRing_Alloc 0x613E10` | ring size | returns 0 |
| Phyre cluster instance lists | `PClusterHeap__bumpAllocate 0x440CF0`, a bump pointer in a cluster block | the block | returns 0 |
| battle units | `FFX_Btl_AllocActorArrays 0x793530`, engine heap | 0x1E270 (31 x 3984) and 0xDCE0 (62 x 912) | fixed |
| ATEL actor pools | `g_ffxAtelActorPool`, `FFX_Tk_AllocSectorRounded` blocks sized by `FFX_Atel_CalcActorPoolSize 0x86A290` | per event | engine heap |

### 2.8 The CRT heap and the third party libraries

| library | allocator | evidence |
|---|---|---|
| FMOD | engine heap through `FfxFmod_MemAlloc`, `_MemRealloc`, `_MemFree` passed to `FMOD_Memory_Initialize` by `FfxFmod__init` | [V 0x7066D0] |
| libvpx, mkvparser, ogg and vorbis | `PhyreMalloc`, `PhyreMallocCount`, `PhyreRealloc`, `PhyreFree` from the table at 0xC6B274..0xC6B29C, installed by `PVideoPlaybackWin32__startVideoDecodingThread` and `PVideoPlaybackWin32_openAudio` | [V]. Their `calloc` slot is `PhyreMallocCount`, which does not zero, so the codecs' `calloc` returns 0xCD bytes [V] |
| Lua (Phyre scripting) | `PScripting_LuaAlloc 0x5D5BA0` -> `PhyreMallocAligned(n, 16)` and `PhyreFree` | [V] |
| Lua (`luaL_newstate`) | `lauxlib_l_alloc 0x94C5F0` -> CRT `realloc` | [V], whether `luaL_newstate` is reached is open |
| Iggy | `IggyInit` gets `{0, sub_A3E5A0, sub_A3E5C0}` from `PUtilityIggy__vf01`, the free callback is `PhyreFree` | [V free side, I alloc side] |
| Bullet | `btAlignedAllocDefault` and `btAlignedFreeDefault` over `sub_9D4C60` (`malloc`) and `free_0`. The setter at 0x9D4D03 is orphan, so the defaults stay. Bullet classes without an aligned `new` use `FFX_Alloc` and `FFX_Free` | [V data at 0xC64BD8..0xC64BE4] |
| vorbis tables, Iggy glue, SG debug windows, config, CRT startup | CRT `malloc` family directly | appendix B |
| Win32 `HeapAlloc` | one small class at 0x9DA05C..0x9DA217, vtable 0xB702A0 | [V], owner not identified |

---

## 3. Phyre's allocator and how FFX's sits on it (item 1.3.3)

**What the SDK says.** In the PhyreEngine 3.21 SDK, `PhyreMalloc` and `PhyreFree` are declared in
`PhyreMemoryBase.h` and defined `PHYRE_WEAK` in `Core/PhyreMemoryFuncs.cpp`, so an application can
replace them. The default on Win32 over-allocates by `sizeof(void *) + alignment` from `malloc` and
stores the base pointer just below the aligned result. `PMemoryBase`'s `operator new` and `delete`
call them. That is a 3.21 reference, the game is 3.9, and none of it was copied. It only told me what
to look for.

**What the binary does.** The same shape, with `FFX_MemAlloc` where the SDK has `malloc`. [V for every
row]

| entry | address | sites | body |
|---|---|---|---|
| `PhyreMalloc(size)` | 0x42FB40 | 113 | `FFX_MemAlloc(size + 20)`, round up to 16, raw pointer in the aligned dword below |
| `PhyreMalloc(size, align)` | 0x42FB80 | 1093 | `FFX_MemAlloc(size + align + 4)`, round up to align, raw pointer below |
| `PhyreMallocCount(count, size)` | 0x42FAE0 | 2, both table entries | `FFX_MemAlloc(count * size + 20)`, 16 aligned, no zeroing |
| `PhyreRealloc(ptr, size)` | 0x42FBD0 | 2, both table entries | `FFX_MemRealloc(raw, size + 20)`, then re-align |
| `PhyreFree(ptr)` | 0x42FB20 | 4314 | null check, `FFX_MemFree(*(aligned dword below ptr))` |

The "aligned dword below" is computed as `((7 - p) & 3) + p - 7`, which is `(p - 4)` rounded down to
4. Phyre classes' deleting destructors (`<Class>__vf00`) call `PhyreFree`, and global `new` and
`delete` reach the same heap through `FFX_Alloc` and `FFX_Free` (section 1 point 3). Whether the
redirect was done by defining the weak symbols or by building Phyre with `malloc` remapped, the
binary cannot say, and it does not matter. [I]

**The picture.**

```
 game code      Phyre classes      std, Bullet new     codecs, Iggy, Lua     FMOD
     |               |                   |                    |               |
 FFX_Alloc       PhyreMalloc*        FFX_Alloc            PhyreMalloc*    FfxFmod_Mem*
 FFX_Malloc      PhyreFree           FFX_Free             PhyreFree           |
 FFX_Free            |                   |                    |               |
     +---------------+-------------------+--------------------+---------------+
                                    |
                     FFX_MemAlloc / FFX_MemFree / FFX_MemAllocAligned / FFX_MemRealloc
                                    |
                 FFX_MemAllocLocked ... (one CRITICAL_SECTION, g_ffxHeapLock)
                                    |
                FFX_Heap_* pools (span up to 768 MB each, 255 size buckets)
                                    |
                     FFX_Vm_Reserve / Commit / Decommit / Release
                                    |
                               VirtualAlloc

 carved out of the engine heap: SG heap (17 MB, dead), cloth clone mspace (1 MB), CHR pool,
 battle arrays, ATEL actor pools, map fx extra heap, prim build buckets
 static buffers: ppp heaps, CHRDATA, menu objects
 CRT malloc: Bullet aligned allocs, vorbis internals, Iggy glue, SG debug windows, config
```

So FFX's heap is not on top of Phyre's. Phyre's allocator is a thin client of FFX's heap, the same as
every other client. Phyre's own sub-allocators (`PClusterHeap`, the render command ring) carve blocks
they got from `PhyreMalloc`.

---

## 4. `FFX_Ch_MoveChrMemory`: the order, the trigger, and why it never runs

### 4.1 The order, as coded

`FFX_Ch_MoveChrMemory 0x826FA0` returns 0 at once unless `g_ffxChrMemoryMoveWanted 0x12FBDA0` is set
[V 0x826FA6]. Otherwise it walks `g_ffxChrArray` from slot 0 to `g_ffxChrCount - 1`, and for each slot
with `m_inUse` set and `m_flags2 & 0xC00` clear it moves, in this order, each through
`FFX_SgMem_MoveBlockDown 0x83C350` [V 0x827000..0x82718F]:

1. `m_joints` (+0x328, first call at 0x827025), then fixes the CHR up with `sub_826980`.
2. `m_channels`, then `sub_839890`.
3. `m_parts`.
4. `m_collisionVolumes`.
5. For each part, `skeleton +6` of them: the part's +8, +4 and +0xC blocks.
6. `m_texAnimBlock`, then `sub_83CDD0`.
7. The texture animation slot's data through `sub_77E9F0` / `sub_77EA10` when `m_texAnimProgram` is set.
8. `m_texListACopy`, then `sub_83CA20` and `sub_83C940`.

If anything moved for that CHR it calls `sub_803610` (re-points the effect link records at the moved
parts) and returns 1, so one call moves at most one CHR [V]. When the CHR walk moves nothing, it
moves the first movable resident motion bundle (`FFX_Mot_MoveBundleMemory 0x837050`, over
`g_ffxMotBundleTable`), then the block `sub_831B70` returns, and clears the wanted flag only when
neither moved [V 0x8271B9]. The caller loops it to completion
(`FFX_Ch_MoveChrMemoryUntilDone 0x827260`).

So the compaction order would have been: CHR slots ascending, within a CHR the eight groups above,
then motion bundles in table order, then the one block from `sub_831B70`, each moved to the lowest
free MCB that fits, searching up from `g_ffxSgMoveCursor`.

### 4.2 Who asks for it

`FFX_Ch_RequestMemoryMove 0x827250` sets the flag, from `FFX_Ch_Dispose`, `FFX_Ch_ModelPurgePacket`,
`sub_831BA0`, `FFX_Ch_ReleaseResource` and `sub_8467B0` [V]. The flag is set after the first CHR
dispose and then stays set, because only a completed move clears it [I from the writers].

### 4.3 Why it never runs

`FFX_Ch_MoveChrMemory` has two callers, both inside `FFX_Ch_MoveChrMemoryUntilDone` [M]. That has two
callers: the orphan SG allocator (section 2.2) and `FFX_SgMem_LargestFreeBlock 0x83BFF0` with mode 2
[V]. `FFX_SgMem_LargestFreeBlock` has one caller, `FFX_Oef2_Op162` sub-op 0x5000, which reads
[V 0x816FFE..0x817037]:

```
816FFE  push 0
817000  call FFX_SgMem_LargestFreeBlock
817008  cmp  eax, 1E0000h
81700D  jge  loc_8170A8          ; enough memory, leave (a2 + 4)
817013  push 1
...                              ; same test with mode 1, then mode 2
```

Mode 0 returns the largest free MCB payload in SG heap 0. Heap 0 never changes after
`FFX_SgMem_InitHeap` (section 2.2), so the answer is always `0x1100000 - 32 = 0x10FFFE0`, which is at
least 0x1E0000, and the signed `jge` at 0x81700D leaves before mode 1 or mode 2 is called. [V]

Two more things would have broken it on PC anyway. `FFX_SgMem_MoveBlockDown` reads SG MCB headers,
and every block a CHR points at is an engine heap block whose +0 is the magic 0xABCDEF12, not a next
pointer [V 0x83C372]. And `FFX_Ch_InitMemoryMoveCursor 0x826230`, called by `FFX_MainInit`, allocates
4 KB, passes it to `FFX_SgMem_ReallocHeap0(p, 0)` which frees it, and then points
`g_ffxSgMoveCursor 0x13019B8` at the freed header [V].

**Consequence.** No block ever moves. A `CHR *` is stable for the life of the pool, and so is every
pointer read out of a CHR, until the CHR or its model is disposed. Item 3.5.2 can stay retired, and
`PORTING_RULES.md` D1 needs no exception: there is no compaction in the original to have an order.

---

## 5. The caller census (item 1.3.2's done-when)

**Method.** [M] Every xref to every entry point in the table below, code and data, minus xrefs from
inside the family itself. 9097 sites. Each site was attributed to the function that contains it, and
each function to a subsystem by its name and then by the `.text` region map in `MAP.md`. Sites in
code that IDA has not made a function of were checked by walking back to the start of their block:
273 are live (the block is reached by a jump or a data pointer) and 1784 are orphan. The orphans are
1562 whole function bodies that no code refers to and whose address appears as no aligned dword in
any data segment (7 unaligned coincidences) [M], so they are dead code left in the image. They are
counted in appendix A and not listed.

**Totals.** Appendix A has every entry point. The engine heap's direct family (`FFX_Alloc`,
`FFX_Malloc`, `FFX_Free`, `FFX_MemAlloc`, `FFX_MemFree` and the wrappers in 2.1) has 3018 sites in
1270 functions. The Phyre layer adds 5524 sites, of which 4715 are inside Phyre, its platform layer
and the static initializers, and 809 (284 functions) are game side. The CRT heap has 388 sites in 93
functions.

**By subsystem.** Appendix B lists every caller of the engine heap family by subsystem, with site
counts and thread tags, and the game side callers of the Phyre layer, the CRT and the small arenas.
The largest engine heap users, by sites and functions, are the PC layer at 0x633000..0x700000 (boot,
FMV, save UI, chr cluster, async load, cloth: 1210 sites in 336 functions), the debug managers (209 in
93, all dev only), FMOD glue (138 in 44), rcBg (136 in 77), battle and save data (125 in 42), and chr,
motion and the main step (115 in 42).

---

## 6. Threads

[M] Reachability over direct calls from each thread's body, as listed in `THREADS_AND_CLOCKS.md`. The
async load roots include the 25 task functions passed to `AsyncLoadManager__enqueue` and
`__enqueueWithContext`, and the vsync roots the handlers passed to `sub_888790`. Indirect calls are
not followed, so these are lower bounds.

| thread | engine heap functions | Phyre layer | CRT | other arenas |
|---|---|---|---|---|
| main (the step) | the rest | the rest | the rest | all of them |
| T1 render | 0 direct | 15 (`PArray_PUInt8__resize`, `Phyre_RenderD3D11_ReleaseTargetViews` and Phyre internals) | 1 | none |
| T4 JobWorker, boot job (`FFX_MainInit`) | 271 | 170 | 29 | ppp 4, op 2 |
| T4 JobWorker, per frame jobs (`FFX_Ch_UpdateRenderJob`, `FFX_Vfx_PrimBuildRunSlots`) | 0 | 0 | 0 | none, the prim build slots are taken on the step thread |
| T5 FmodManager | 38 | 22 | 1 | none |
| T6 input | 22 | 22 | 1 | none |
| T7 async load | 116 | 164 | 11 | none |
| T9 vsync | 0 | 0 | 0 | none |
| T10, T11 video | 0 direct, but the codecs allocate through the Phyre table in 2.8 [V] | | | none |
| FMOD's own threads | through `FfxFmod_Mem*` [I] | | | none |

Every thread that allocates uses the engine heap or the CRT, both locked. The unlocked arenas (ppp,
Oef2, menu objects, prim build, the SG heap) are reached only from the step thread and from the boot
job, which runs while the main thread waits on the job ring [M]. The cloth clone pool is locked and
step thread only.

---

## 7. Parity: what the recreation must reproduce and what it may replace

**Must reproduce.**

1. **`FFX_Oef2_Op162` sub-op 0x5000 never takes its low memory branch.** It is a script visible read of
   allocator state, and on PC the state is the constant 0x10FFFE0. The recreation implements the op as
   "enough memory, continue at a2 + 4" and never compacts. [V 0x81700D]
2. **Uninitialized engine heap memory reads as 0xCD.** Any simulation field the original forgets to
   initialize reads 0xCDCDCDCD, the same on every machine. A recreation that zeroes or leaves garbage
   would diverge on any such read. Cheapest parity: fill new simulation side blocks with 0xCD, at
   least until a uninitialized read census says which ones matter. [V fill, I that a reader exists]
3. **The fixed pool counts and their full behaviour** (2.7), already the subject of `PORTING_RULES.md`
   D2 and item 3.5.3. The ppp heap's exhaustion flag is in the same class but is presentation unless a
   ppp result reaches the simulation [I].
4. **No "low memory" behaviour anywhere else.** The engine heap never reports failure, it crashes. So
   there is no handled failure path in the engine heap for D1 to keep, and the recreation should treat
   any engine heap failure as fatal too.

**May replace freely.**

- The engine heap itself, the pools, the buckets, the page layer, the spans. No game logic reads block
  addresses, block order or fragmentation [M, the only allocator state any live code reads is the SG
  heap query in point 1, plus `FFX_Mem_BlockSize` for buffer sizing in `FFX_Ch_BlkAllocate` and the
  FMOD realloc].
- `FFX_Ch_MoveChrMemory`, the whole SG heap, `FFX_Mem_SetNextAllocAlign` and the other `mem.c`
  setters: dead or no-ops.
- The `FFX_Free` tracker, the memory report, the dlmalloc cookie (`THREADS_AND_CLOCKS.md` C1).
- Phyre's wrappers, FMOD's, the codecs', Lua's, Iggy's and Bullet's allocators.
- Alignment: the original gives 16 everywhere except the explicit aligned entries. Asking for more
  is harmless.

**Watch.**

- **`FFX_MemAllocAlignedLocked` leaves up to `align - 1` stale bytes at the end of the payload**
  (2.1). Stale contents depend on heap history, and heap history differs between machines because
  the async load thread allocates concurrently with the step. If anything in the simulation reads
  those bytes the original itself desyncs between peers [I]. The recreation should fill them.
- **`save:84` hands a raw heap pointer to a script** (`ATEL_MOVIE_SAVE_LIBRARIES.md` 2.5), only in
  the dev package test22. A recreation returns a handle, and the oracle must not compare that value.
- **The codecs' `calloc` returns 0xCD bytes, not zeroes** (2.8). Presentation only, but a recreation
  that zeroes may decode a damaged stream differently from the original [I].
- **`FFX_Alloc(0xFFFFFFFF)` from an overflowing `new[]`** crashes in the `memset` rather than throwing
  [V 0x6FB910]. Only reachable with corrupt counts.

---

## 8. Corrections made to other docs

1. `BOOT_SEQUENCE.md` steps 77 and 78 called the 1 MB block "a 1 MB arena" and its allocator
   "the allocator over that arena". It is the cloth clone pool, a dlmalloc mspace (2.4).
2. `BOOT_SEQUENCE.md` step 86 called `sub_83C200` "the scratch arena" with `&unk_1100000`. It is
   `FFX_SgMem_Init`, a 0x1100000 byte SG heap that nothing allocates from (2.2).
3. `FFX_GAME_NOTES.md` (the CHR notes), `PHASE1_CLONE.md` (pointer lifetime), `HELD_OBJECTS.md` and
   `CHARACTER_MOTION.md` said `FFX_Ch_MoveChrMemory` compacts the blocks a CHR points at, so pointers
   read out of a CHR must be re-read. It never runs in this build (4.3), so those pointers are stable
   until the CHR or model is disposed.
4. `ATEL_BTL_LIBRARY.md` named `FFX_Arena_InitCookieFromTickCount 0x6EA050` and an "arena header
   cookie". It is dlmalloc `init_mparams`, now `FFX_Dl_InitMparams`, and the value is the mspace
   magic.
5. `THREADS_AND_CLOCKS.md` row C1 called the same value "the header cookie of the 1 MB arena
   allocator". Reworded to the dlmalloc magic of the cloth clone pool.

Not edited because they are outside this folder: `MAP.md` in the recreation says "Phyre has its own
allocator underneath" in the memory row. It is the other way round (section 3). The 1.3.1 wording
"PS2 heap header" names the PC engine heap (2.2 naming note).

---

## 9. Open items

- Owner of the `HeapAlloc` class at 0x9DA05C..0x9DA217 (vtable 0xB702A0).
- Whether `luaL_newstate` (CRT backed Lua) is reached in retail, and Iggy's alloc callback
  `sub_A3E5A0`.
- An uninitialized read census of simulation structs, to decide where the 0xCD fill of 7.2 is
  actually load bearing.
- Whether any std container in simulation code is keyed or ordered by pointer value. None was seen,
  but no systematic scan was made.
- The vsync and video thread rows are lower bounds, their work is reached through function pointers.

## 10. Names

`tools/apply_memory_names.py`: 66 functions, 1 function rename, 15 globals (one of them a rename).
Rehearsed on a copy of the IDB: dry run 82 planned and 0 skipped, apply with comments 82 renamed and 0
failed with every name and comment read back, a second dry run planned 0.

---

## Appendix A. Entry point totals

"live outside a function" counts sites in code IDA has not wrapped that is reached by a jump or a data
pointer. "orphan" counts sites in dead code (section 5 method).

| arena | entry | address | sites | functions | live outside a function | orphan |
|---|---|---|---|---|---|---|
| crt | free | 0x9495F2 | 115 | 32 | 6 | 10 |
| crt | free imp | 0xB0C370 | 88 | 27 | 8 | 16 |
| crt | malloc | 0x949D80 | 76 | 21 | 2 | 15 |
| crt | malloc imp | 0xB0C468 | 44 | 17 | 2 | 10 |
| crt | calloc | 0x949D3E | 41 | 18 | 3 | 1 |
| crt | realloc | 0xA8D73C | 10 | 8 | 1 | 0 |
| crt | realloc imp | 0xB0C578 | 3 | 2 | 0 | 0 |
| crt | bullet malloc | 0x9D4C60 | 3 | 1 | 1 | 1 |
| crt | bullet free | 0x9D4C70 | 3 | 1 | 1 | 1 |
| crt | calloc imp | 0xB0C43C | 2 | 1 | 0 | 0 |
| crt | _recalloc imp | 0xB0C440 | 2 | 1 | 0 | 0 |
| crt | _recalloc | 0x949D44 | 1 | 1 | 0 | 0 |
| fmod | FfxFmod_MemAlloc | 0x7066D0 | 1 | 1 | 0 | 0 |
| fmod | FfxFmod_MemRealloc | 0x706730 | 1 | 1 | 0 | 0 |
| fmod | FfxFmod_MemFree | 0x706710 | 1 | 1 | 0 | 0 |
| heap | FFX_Free | 0x630390 | 1407 | 799 | 2 | 326 |
| heap | FFX_MemFree | 0x687010 | 452 | 188 | 16 | 67 |
| heap | FFX_Alloc | 0x630310 | 338 | 187 | 1 | 101 |
| heap | FFX_MemAlloc | 0x687020 | 333 | 144 | 31 | 16 |
| heap | FFX_Malloc | 0x6304C0 | 205 | 54 | 1 | 5 |
| heap | rcbg free | 0x9258E0 | 55 | 33 | 1 | 2 |
| heap | rcbg alloc | 0x9258A0 | 44 | 36 | 0 | 2 |
| heap | aligned alloc | 0x686FF0 | 36 | 13 | 1 | 6 |
| heap | FFX_Tk_Free | 0x88D520 | 29 | 12 | 2 | 4 |
| heap | map alloc | 0x90BBD0 | 23 | 6 | 11 | 1 |
| heap | map free | 0x90BBC0 | 20 | 8 | 3 | 4 |
| heap | sys aligned alloc | 0x83F0A0 | 14 | 9 | 0 | 1 |
| heap | FFX_Sys_FreeAligned | 0x83F090 | 12 | 7 | 0 | 1 |
| heap | aligned free | 0x686FE0 | 10 | 8 | 0 | 0 |
| heap | FFX_Ch_BlobBlockSize | 0x83C050 | 9 | 6 | 0 | 1 |
| heap | FFX_Tk_AllocSectorRounded | 0x88E650 | 7 | 6 | 0 | 1 |
| heap | rcbg calloc | 0x9258B0 | 6 | 4 | 0 | 2 |
| heap | realloc | 0x687060 | 3 | 1 | 1 | 1 |
| heap | FFX_Map_AllocAligned64 | 0x90BBE0 | 3 | 2 | 1 | 0 |
| heap | map alloc flagged | 0x90BC10 | 2 | 1 | 0 | 0 |
| heap | calloc locked | 0x6FB820 | 1 | 0 | 0 | 1 |
| heap | free on a given heap | 0x6FB5D0 | 1 | 1 | 0 | 0 |
| heap | sys alloc | 0x83F100 | 1 | 1 | 0 | 0 |
| heap | sg realloc 0 | 0x83F010 | 1 | 1 | 0 | 0 |
| heap | sg realloc 1 | 0x83F030 | 1 | 1 | 0 | 0 |
| heap | rcbg free thunk | 0x922540 | 1 | 1 | 0 | 0 |
| heap | rcbg free thunk | 0x922B40 | 1 | 1 | 0 | 0 |
| heap | rcbg free thunk | 0x922F30 | 1 | 1 | 0 | 0 |
| heap | magicfile aligned free | 0x9DBE80 | 1 | 0 | 0 | 1 |
| heap | magicfile aligned alloc | 0x9DBE90 | 1 | 0 | 0 | 1 |
| lua | PScripting_LuaAlloc | 0x5D5BA0 | 3 | 2 | 1 | 0 |
| menuobj | FFX_MenuObj_Alloc | 0x8AA1A0 | 40 | 40 | 0 | 0 |
| op | op_malloc | 0x7FF780 | 22 | 15 | 0 | 0 |
| op | op_free | 0x7FF1D0 | 11 | 8 | 0 | 0 |
| phyre | PhyreFree | 0x42FB20 | 4314 | 2829 | 150 | 779 |
| phyre | PhyreMalloc(size,align) | 0x42FB80 | 1093 | 482 | 33 | 385 |
| phyre | PhyreMalloc(size) | 0x42FB40 | 113 | 62 | 5 | 2 |
| phyre | PhyreMalloc(count,size) | 0x42FAE0 | 2 | 0 | 2 | 0 |
| phyre | PhyreRealloc | 0x42FBD0 | 2 | 0 | 2 | 0 |
| ppp | pppMemAlloc | 0x716A30 | 31 | 17 | 0 | 1 |
| ppp | FFX_Ppp_HeapFreeBlock | 0x716A90 | 29 | 15 | 0 | 1 |
| ppp | FFX_Ppp_InitHeapForSet | 0x7169D0 | 7 | 6 | 0 | 1 |
| ppp | FFX_PppHeap_Init | 0x72C480 | 6 | 5 | 0 | 1 |
| ppp | FFX_PppHeap_Alloc | 0x72C3D0 | 3 | 1 | 0 | 1 |
| ppp | FFX_PppHeap_Free | 0x72C510 | 3 | 2 | 0 | 0 |
| primbuild | FFX_Vfx_PrimBuildAllocSlot | 0x728660 | 9 | 8 | 0 | 0 |
## Appendix B. Callers by arena and subsystem

Thread tags after a function: B boot job, A async load, F FMOD, I input, R render. No tag means the step thread or unreached by the direct call walk (section 6). Unnamed functions are given by address.

Engine heap, every caller of the FFX_Alloc family: 3018 sites, 1270 functions.

**FFX PC layer (boot, fmv, save, chr cluster, async load)** (914 sites, 223 functions, plus 16 sites in live code outside a function and 253 in orphan code)
: 0x633240, 0x6335A0 [AB], 0x633FE0 [ABFI], 0x634220 x3 [ABFI], 0x6343B0 [AB], 0x6343E0 [ABFI], 0x634610 x5 [ABFI], 0x634850 x39 [ABFI], 0x635680 x2, 0x635700 x2, 0x635780 x2, 0x6357F0 x2, 0x635C40 [ABFI], 0x635C80 x2 [ABFI], 0x635CE0 x2 [ABFI], 0x636280 [AB], 0x636310 [AB], 0x636E20 x2 [ABFI], 0x637190 x2 [ABFI], 0x641510 x2, 0x645AA0, 0x646920 x3, 0x649A60 [AB], 0x649FF0, 0x64A130 x2 [A], 0x64A360 x2 [A], 0x64BDE0 [A], 0x64CEC0 x3, 0x64D050 [AB], 0x64D9E0 x2 [AB], 0x654960 x2, 0x654D40 [AB], 0x654DB0 [B], 0x654E20 [AB], 0x654EA0 [AB], 0x654ED0, 0x654F00, 0x654F40 [AB], 0x654F80 [A], 0x655120 x2 [A], 0x655460 x3, 0x655920 x2 [A], 0x6559A0 x2, 0x655A20 x3, 0x655B20 x3 [A], 0x655E80 [B], 0x6565E0 x3, 0x656A00 x3, 0x657A50 x8 [A], 0x659D20 x9, 0x65B750, 0x65B820, 0x65D240 x12 [B], 0x65DA80 x2, 0x65FCA0 [B], 0x665F60 [B], 0x666920 x2, 0x666AA0 x2, 0x667BB0 x4 [A], 0x667E00 x3 [AB], 0x667E80 x3 [AB], 0x667F00 x3 [B], 0x667F80 x3 [A], 0x668000 x3 [A], 0x668080 x3 [AB], 0x668100 x3, 0x668180 x3 [B], 0x668200 x3 [AB], 0x668280 x3 [AB], 0x668300 x3 [A], 0x668380 x3 [A], 0x66ED00 x2 [B], 0x66FB70 x6 [B], 0x670960 x4 [B], 0x672070, 0x67B6C0 x2 [A], 0x67B740 [A], 0x67B770 x2 [A], 0x67B820, 0x680000, 0x680490 [A], 0x680970 x16, 0x681970 x2, 0x681AD0 x3 [A], 0x681C10 x116, 0x686BB0 [A], 0x686DA0 x2, 0x6870B0, 0x687210, 0x6872E0, 0x687370 x2, 0x6874B0 x6, 0x687C10 x2, 0x6897D0, 0x689830, 0x68B910, 0x68BA20 x2, 0x68BAE0, 0x68BBA0, 0x68BD00 x2, 0x68BF40, 0x68CF60 x2 [I], 0x68D0E0 x2 [I], 0x68D2D0 x2, 0x68D330 x2, 0x68D9F0 [I], 0x68DA80 [I], 0x68DB80 x3, 0x68E2B0, 0x68E4A0, 0x690760 x2, 0x692A90 x2, 0x692B90, 0x693D70 [A], 0x6993D0 [AB], 0x6995E0 x2 [AB], 0x699AB0, 0x699CF0, 0x699D60 x2, 0x69A060 x2, 0x69A0E0, 0x69A1B0, 0x69A1E0 [AB], 0x69A2D0 [B], 0x69A6B0 [B], 0x69A970 [B], 0x69A9C0 x2, 0x69ABA0 [AB], 0x69AE20 [ABFI], 0x69B010 [ABFI], 0x69C6B0 x2 [A], 0x69C8E0 [B], 0x69CAF0, 0x69CD40 x2 [A], 0x69CFE0 x2 [B], 0x69DA20 [A], 0x69E140 x6, 0x69E770, 0x69E900, 0x69EDD0, 0x69EF00, 0x69EFA0, 0x69EFD0, 0x69F540, 0x69F5C0, 0x69FE50, ??1TaskStack@details@Concurrency@@QAE@XZ_14, ??1TaskStack@details@Concurrency@@QAE@XZ_15, ??1TaskStack@details@Concurrency@@QAE@XZ_16, ??1TaskStack@details@Concurrency@@QAE@XZ_17, ??1TaskStack@details@Concurrency@@QAE@XZ_18, ??1TaskStack@details@Concurrency@@QAE@XZ_19, ??1TaskStack@details@Concurrency@@QAE@XZ_20, ??1TaskStack@details@Concurrency@@QAE@XZ_21, ??1TaskStack@details@Concurrency@@QAE@XZ_22, ??1TaskStack@details@Concurrency@@QAE@XZ_23, ??1TaskStack@details@Concurrency@@QAE@XZ_24, ??1TaskStack@details@Concurrency@@QAE@XZ_25, ??1TaskStack@details@Concurrency@@QAE@XZ_26, ??1TaskStack@details@Concurrency@@QAE@XZ_28, ??1TaskStack@details@Concurrency@@QAE@XZ_29, ??1TaskStack@details@Concurrency@@QAE@XZ_30, ??1TaskStack@details@Concurrency@@QAE@XZ_8, AsyncControllerTaskManager__createSingleton x2, AsyncControllerTaskManager__ctor x3, ClassCharacter__createInstance x4 [B], ClassCharacter__dtor, ClassCharacter__loadFromFile x9 [A], ClassCharacter__scalar_deleting_dtor, ClassCharacter__unload [B], ClassCharacterTable__create x2, ClassCharacterTable__destroy x2, ClassVFXRenderDataTable__ClearByType, FFEscMenu__createSingleton x2, FFX__graphicFinalize x4, FFX_Boot_FirstAsyncLoad x2 [A], FFX_Chr_AttachModelInstance [B], FFX_Chr_CancelPendingAttachments [B], FFX_Chr_CloneClassCharacterVertexData x11, FFX_Chr_FinishCharacterLoad x19 [A], FFX_Chr_FreeClonedClassCharacter x4 [B], FFX_Chr_ProcessPendingAttachments, FFX_Chr_ReleaseInstance [B], FFX_ChrShadow_CreateBlobMesh x3 [A], FFX_EnqueueCharacterLoad [B], FFX_File_Close x2 [ABF], FFX_File_Open x2 [ABF], FFX_File_ReadWholeIfExists x2, FFX_Frame_GetSuspendState, FFX_Frame_RenderScene x4, FFX_Frame_SetCutsceneSuspendGlobal, FFX_Gfx_BeginMapLoad x3 [B], FFX_Gfx_CreateMeshInstance x3 [B], FFX_Gfx_FinishMapLoad x5 [B], FFX_Gfx_InstallPendingMapScene, FFX_Gfx_ReleaseChrMeshRecords x2 [B], FFX_Gfx_UnloadCharacterClusters x6 [B], FFX_Glue_ReleaseMagic154_422Buffers x2, FFX_GraphicInitialize x5, FFX_InputThread__main x2 [I], FFX_LoadCharacterData [B], FFX_PoolSet_createSingleton x2, FFX_SaveUtil_AllocState, FFX_SaveUtil_FreeState, FFX_SyncData_Create x2, FFX_SyncData_CreateAtInit, FFX_SyncData_Destroy, FFX_TexList_LoadAndEnqueue x9 [AB], FFX_TexList_LoadAndEnqueueA x7 [A], FFX_TexList_Release x12 [B], FFX_Vfx_CreateModel x2 [B], FFX_Vfx_SetModelParts x2 [B], FFX_VsyncEmu_StartThread [B], GrowDwordVector x3 [AB], GrowDwordVector_1 x3 [AB], GrowDwordVector_2 x3, maybe_FFX_Gfx_BindGameCameraToPhyre x2, PostProcessTweakUtil__createSingleton x2, PostProcessTweakUtil__scalar_deleting_dtor, Request__scalar_deleting_dtor, RequestPool_LoadClusterRequest__ctor x2, Thread__vf00, VsyncThreadImpl__scalar_deleting_dtor.

**FFX PC layer (fmv, esc menu, cloth, booster, dll, mem)** (296 sites, 113 functions, plus 6 sites in live code outside a function and 81 in orphan code)
: 0x6C0560, 0x6C1420 x2, 0x6C14A0, 0x6C19B0 x2 [B], 0x6C1C50 x2, 0x6C2120, 0x6C2190 x2, 0x6C2210, 0x6C2FF0 x4 [A], 0x6C32D0 x4, 0x6C3550, 0x6C3CD0 x2, 0x6C4820 x2, 0x6C4A30 x2, 0x6C4FB0, 0x6C5030 x2, 0x6C50B0, 0x6C5790, 0x6C57C0, 0x6C9500 x2, 0x6D4D30 x2, 0x6D4DB0, 0x6D5CA0 [A], 0x6D5DC0 x2 [A], 0x6D5FF0 x2 [A], 0x6D6C70, 0x6D6D00 x7, 0x6D7370, 0x6D78C0, 0x6D78F0, 0x6D7930 [A], 0x6D7980 x2 [A], 0x6D79C0 x2, 0x6D8A00 x2, 0x6DAF30 x3, 0x6DAFC0 x2, 0x6DB2F0 [B], 0x6DDC80 x2 [A], 0x6DDFD0, 0x6DE1D0, 0x6DE530, 0x6DE720 x5, 0x6DE860 x2, 0x6DEB90 x2, 0x6DEC20, 0x6E0AB0 x4, 0x6E2AC0, 0x6E3240, 0x6E32A0, 0x6E4470 x2 [B], 0x6E5490, 0x6E56A0, 0x6E56D0, 0x6E76C0, 0x6E76F0, 0x6E8690 x2, 0x6E8700 x2, 0x6E8970, 0x6E8A50 x2 [AB], 0x6E8C80 x2 [AB], 0x6E9250 x2, 0x6E93F0 x2, 0x6E9590, 0x6E9830 x2 [AB], 0x6ED9E0, 0x6EE570, 0x6F3130 x2, 0x6F39C0 x2 [AB], 0x6F3AE0 [AB], 0x6F3EE0 x2 [A], 0x6F4200, 0x6F4410 [AB], 0x6F4530, 0x6F45E0, 0x6F46E0, 0x6F4790, 0x6F50A0 x7, 0x6F54F0 x2, 0x6F6DC0 x4 [A], 0x6F8C50 x6 [A], 0x6FEDF0 x2, 0x6FFC30, BlurEffect__createSingleton x2, BlurEffectEx__createSingleton x2, crossThreadImpl__scalar_deleting_dtor, FFX_Ach_Create x2, FFX_Booster_DestroyAllIcons, FFX_Booster_Disable, FFX_Booster_HideIcon, FFX_Booster_HudDtor_ORPHAN, FFX_Booster_HudInit, FFX_Booster_HudShutdown x2, FFX_EscMenu_BuildVideoPage x2, FFX_EscMenu_OnConfirmKey, FFX_EscMenu_OnVideoSettingRefresh, FFX_Lang_DestroyManager, FFX_Mutex_Create [B], FFX_SaveFile_BeginSave, FFX_SaveMgr__registerStaging, FFX_SaveMgr_Create, FFX_Sema_Create x2, FFX_SyncData__clearMapTables [B], FFX_SyncData__init x10, FFX_SyncData__loadSyncDataList [A], FFX_SyncData__loadSyncList [B], FFX_SyncData__loadSyncVoiceList [A], FFX_SyncData__readWholeFile [AB], FFX_SyncData__release x11, FFX_Thread_Create x2, FFX_Thread_TerminateAndFree [AFI], JobSchedule__initRings x2, JobSchedule__startWorkers x2, PhyFMVPlayerManager__createSingleton x2.

**Phyre (by name)** (244 sites, 227 functions)
: PAnimationChannel_const___unsigned_int___vf00, PAnimationChannelTarget_char_const_____vf00, PAnimationChannelTarget_unsigned_int___vf00, PAnimationChannelTargetBind__vf00, PAnimationChannelTargetBindDataMember__vf00, PAnimationChannelTargetBindFunction__vf00, PAnimationChannelTargetBindMethod__vf00, PAnimationChannelTargetBindScene__vf00, PAnimationChannelTargetBindSceneToMeshInstance__vf00, PAnimationChannelTargetBindShaderParameter__vf00, PAnimationClip_float___vf00, PAnimationClip_unsigned_int___vf00, PAnimationClip_void_char_const_____vf00, PAnimationConstantChannel_const___unsigned_int___vf00, PAnimationHierarchyCountNodes__vf00, PAnimationHierarchyPopulateArrays__vf00, PAnimationHierarchyVisitContext__vf00, PAnnotation__vf00, PApplication_double___vf00, PApplication_float_char_const_____vf00, PApplication_int___vf00, PApplication_unsigned_int___vf00, PApplicationScript__vf00, PApplicationScriptBuffer__vf00, PApplicationScriptProxy__vf00, PApplicationViewport___unsigned_int___vf00, PApplicationViewport_float___vf00, PApplicationViewport_unsigned_int___vf00, PAssetHeader__parse x9 [A], PAssetHeader__symbol [AB], PAssetReference___vf00, PAsyncProcessBatch__vf00, PAsyncProcessBuffer__vf00, PAvoidOutputOverwritingInput__vf01, PBase___char_const_____vf00, PBufferAcceptance__vf01, PBulletDebug__vf00, PCaller__vf00, PCallerScript__vf00, PCameraControllerComponent_void___vf00, PCameraControllerComponent_void_float___vf00, PCameraControllerComponent_void_float_float___vf00, PCameraProjection_void_float___vf00, PCheckVisibleBoundsJob__vf00, PClassDescriptor_const_____vf00, PClassDescriptor_const_____vf00_2, PClassDescriptor_const_____vf00_3, PClassDescriptor_const_____vf00_4, PClassDescriptor_const_____vf00_5, PClassDescriptor_const_____vf00_6, PClassDescriptor_const_____vf00_7, PClassDescriptor_const_____vf00_8, PClassDescriptor_const___char_const_____vf00, PClassDescriptor_const___unsigned_int___vf00, PClassDescriptor_const___unsigned_int___vf00_2, PClassLayoutInterface__vf00, PCluster_____vf00, PComponent_____vf00, PContextSwitch__vf00, PContextSwitchInstancing__vf00, PContextSwitchLights__vf00, PContextSwitchLODBlend__vf00, PContextSwitchShader16BitIndices__vf00, PContextSwitchShaderLODLevel__vf00, PContextSwitchShaderOrthoCamera__vf00, PDeleteChildrenVisitor__vf00, PEntity_____vf00, PFindNestedClassVisitor__vf00, PGatherHeaderClassObjectsVisitor__vf00, Phyre__PApplication__onInit x4, Phyre_Bullet_CreateThread_dead x6, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PFunctionCallerConcrete_class_Phyre_PTypedObject___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimatableComponent_class_Phyre_PAnimation_PAnimationSet_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimationClip_class_Phyre_PString_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PAssetReference_class_Phyre_PString_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PAssetReference_class_Phyre_PTypedObject___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PCamera_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PEntity_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PGameplay_PAttachableComponent_class_Phyre_PMatrix4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PGameplay_PAttachableComponent_class_Phyre_PTypedObject___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PGameplay_PCameraControllerComponent_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PGameplay_PLocator_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PGameplay_PScriptedComponent_enum_Phyre_PResult___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PGameplay_PSpline_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PGameplay_PTrigger_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PLOD_PLODGroup_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_POccluderGeometry_POccluderGeometryInstance_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsCharacterCamera_class_sce_Vectormath_Scalar_Aos_Vector3_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsRigidBody_enum_Phyre_PResult___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsRigidBodyBase_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsRigidBodyBase_enum_Phyre_PResult___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PRendering_PLight_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PRendering_PMeshInstance_class_Phyre_PRendering_PMeshInstanceBounds_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PRendering_PMeshInstance_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PScene_PNode_class_Phyre_PMatrix4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PScene_PNode_class_Phyre_PString_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PScripting_PAsyncProcessHeader_class_Phyre_PTypedObject___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation0_PMethodCallerConcrete_class_Phyre_PSprite_PSpriteCollection_enum_Phyre_PResult___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PFunctionCallerConcrete_class_Phyre_PEntity___class_Phyre_PTypedObject___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PFunctionCallerConcrete_class_Phyre_PEntity___class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimatableComponent_class_Phyre_PAnimation_PAnimationClip___unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimatableComponent_enum_Phyre_PResult_char_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimatableComponent_enum_Phyre_PResult_class_Phyre_PAnimation_PAnimationClip_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimatableComponent_enum_Phyre_PResult_class_Phyre_PAnimation_PAnimationSet_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimatableComponent_enum_Phyre_PResult_unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimationClip_enum_Phyre_PResult_unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimationSet_class_Phyre_PAnimation_PAnimationClip___unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimationSet_enum_Phyre_PResult_class_Phyre_PAnimation_PAnimationClip_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimationSet_enum_Phyre_PResult_class_Phyre_PCluster_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PCameraOrthographic_void_class_sce_Vectormath_Scalar_Aos_Vector4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PCluster_enum_Phyre_PResult_class_Phyre_PBase_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PCluster_void_class_Phyre_PCluster_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PEntity_void_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PGameplay_PAttachableComponent_void_class_Phyre_PMatrix4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PGameplay_PCameraControllerComponent_void_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PGameplay_PCameraControllerComponent_void_class_sce_Vectormath_Scalar_Aos_Vector3_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PGameplay_PLocator_enum_Phyre_PResult_char_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PGameplay_PLocator_void_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PInputs_PInputAction_class_Phyre_PBase___unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PMatrix4x3_class_Phyre_PMatrix4x3___class_sce_Vectormath_Scalar_Aos_Vector3_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsRigidBody_void_class_sce_Vectormath_Scalar_Aos_Vector3_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsRigidBodyBase_void_class_sce_Vectormath_Scalar_Aos_Vector3_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PPostProcessing_PPostEffectManager_class_Phyre_PBase___unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PRendering_PLight_void_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PRendering_PLight_void_class_sce_Vectormath_Scalar_Aos_Vector4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PRendering_PMeshInstance_void_class_Phyre_PRendering_PMeshInstanceBounds_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PRendering_PMeshInstance_void_class_sce_Vectormath_Scalar_Aos_Vector4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PRendering_PSamplerStateBase_enum_Phyre_PResult_unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PScene_PNode_void_class_Phyre_PMatrix4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PScene_PNode_void_class_Phyre_PWorldMatrix_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PSprite_PSpriteAnimationInfo_enum_Phyre_PResult_unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PSprite_PSpriteCollection_enum_Phyre_PResult_unsigned_int___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation1_PMethodCallerConcrete_class_Phyre_PWorldMatrix_void_class_Phyre_PMatrix4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation2_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimatableComponent_enum_Phyre_PResult_unsigned_int_class_Phyre_PAnimation_PAnimationClip_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation2_PMethodCallerConcrete_class_Phyre_PAnimation_PAnimatableComponent_enum_Phyre_PResult_unsigned_int_float___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation2_PMethodCallerConcrete_class_Phyre_PGeometry_PMaterialSet_enum_Phyre_PResult_unsigned_int_class_Phyre_PRendering_PMaterial_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation2_PMethodCallerConcrete_class_Phyre_PRendering_PMeshInstance_void_unsigned_int_class_Phyre_PMatrix4_const_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation2_PMethodCallerConcrete_class_Phyre_PSprite_PSpriteCollection_enum_Phyre_PResult_unsigned_int_float___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation3_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsWorld_int_class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector3_const___enum_Phyre_PPhysics_PPhysicsRigidBodyBase_PCollisionGroup___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation4_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsWorld_class_Phyre_PTypedObject_class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector4___class_sce_Vectormath_Scalar_Aos_Vector4_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation4_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsWorld_int_float_class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector3_const___enum_Phyre_PPhysics_PPhysicsRigidBodyBase_PCollisionGroup___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation5_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsWorld_class_Phyre_PTypedObject_class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector4___class_sce_Vectormath_Scalar_Aos_Vector4___enum_Phyre_PPhysics_PPhysicsRigidBodyBase_PCollisionGroup___vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation5_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsWorld_class_Phyre_PTypedObject_float_class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector4___class_sce_Vectormath_Scalar_Aos_Vector4_____vf00, Phyre_PInternal_PCallerImplementation_PCallerImplementation6_PMethodCallerConcrete_class_Phyre_PPhysics_PPhysicsWorld_class_Phyre_PTypedObject_float_class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector3_const___class_sce_Vectormath_Scalar_Aos_Vector4___class_sce_Vectormath_Scalar_Aos_Vector4___enum_Phyre_PPhysics_PPhysicsRigidBodyBase_PCollisionGroup___vf00, PIChildClassVisitor__vf00, PIClassDataMemberDynamicType__vf00, PInputAxisSemantic__vf00, PInputChannelSemantic__vf00, PInputDevice__vf00, PInputJoypadButtonSemantic__vf00, PInputKeySemantic__vf00, PInputMapper_____vf00, PInputMapper_unsigned_int___vf00, PInputMouseButtonSemantic__vf00, PInputTypeSemantic__vf00, PInstanceListConvert__vf01, PLightType__vf00, PLocator_char_const_____vf00, PLocatorShapeType__vf00, PLODBlendType__vf00, PLODMetricType__vf00, PMaterial___unsigned_int___vf00, PMaterialSet_____vf00, PMaterialSet_unsigned_int___vf00, PMatrix4x3_____vf00, PMesh_____vf00, PMesh_int_char_const_____vf00, PMeshInstance___char_const_____vf00, PModifiedBulletKinematicCharacterController__vf00, PNameComponent___char_const_____vf00, PNamespace_____vf00, PNetworkOptimizeBufferAllocator__vf00, PNode_____vf00, PNode___char_const_____vf00, PNode_const_____vf00, PNode_void_char_const_____vf00, PPhysicsCharacterControllerBase_bool___vf00, PPhysicsCharacterControllerBase_float___vf00, PPhysicsPlaneBullet__vf00 x2, PPhysicsRigidBodyBase_bool___vf00, PPhysicsRigidBodyBase_float___vf00, PPhysicsRigidBodyBase_void___vf00, PProcessorProfileTimelineBase__vf00, PProcessorProfileTimelineCpu__vf00, PRendererThreadPoolJob__vf00, PRingBufferedThreadPoolJob__vf00, PSamplerStateFilterFormatType___vf00, PSamplerStateWrapFormatType___vf00, PScheduler_____vf00, PScript_const_____vf00, PScriptableComponent_____vf00, PScriptedComponent_bool___vf00, PScriptMemoryAllocatorBase__vf00, PScriptMemoryAnalyzer__vf00, PScriptMemoryRecorder__vf00, PSerializedClassLayoutInterface__vf00, PShadowCasterType__vf00, PSpriteAttributes_____vf00, PString_char_const_____vf00, PTextureCommonBase_char_const_____vf00, PThreadPool__vf01, PThreadPoolImmediate__vf01, PThreadPoolJob__vf00, PThreadPoolWorkStealing__vf01, PThreadPoolWorkStealing__vf02, PTTYCallback__vf00, PUtility__vf00, PUtilityAnimation__vf00, PUtilityDeveloperExtensions__vf00, PUtilityDynamicGeometry__vf00, PUtilityGame__vf00, PUtilityGameplay__vf00, PUtilityGeometry__vf00, PUtilityIggy__vf00, PUtilityInputs__vf00, PUtilityLOD__vf00, PUtilityPhysics__vf00, PUtilityPostProcessing__vf00, PUtilityRendering__vf00, PUtilityScene__vf00, PUtilityScripting__vf00, PUtilitySerialization__vf00, PUtilitySprite__vf00, PUtilityText__vf00, PWorld_const_____vf00, PWorld_unsigned_int_char_const_____vf00, PWorldMatrix_const_____vf00.

**FFX debug managers** (209 sites, 93 functions, plus 0 sites in live code outside a function and 45 in orphan code)
: 0x6A0070 x2, 0x6A0100 x7 [A], 0x6A0360, 0x6A0390, 0x6A03C0 [A], 0x6A0400 [B], 0x6A0480 x2 [A], 0x6A04E0 x2, 0x6A0540 [B], 0x6A09B0 x2 [B], 0x6A0FE0 [B], 0x6A1230, 0x6A1720 x2 [B], 0x6A1A70 [B], 0x6A1D90, 0x6A3100 x2, 0x6A4240 x2, 0x6A6D50 x2, 0x6A6E00, 0x6A91E0 [A], 0x6ABE40, 0x6ABEB0, 0x6AD2D0 x2, 0x6AD380 x2, 0x6AD430 x2, 0x6AD600 x2, 0x6AD6B0 x2, 0x6AD760 x2, 0x6AD810 x2, 0x6AD8C0 x2, 0x6AD970 x2, 0x6ADA20 x2, 0x6ADB80 x2, 0x6ADC90, 0x6ADF70 x2 [B], 0x6AE0C0 x2, 0x6AE240 x2, 0x6AE340 x2 [B], 0x6AF420 x2 [B], 0x6B3FE0 [B], 0x6B52A0, 0x6B5930, 0x6B5A70 x2, 0x6B5CB0 x2, 0x6B5D30 x2, 0x6B5DB0 x2, 0x6B5E30 x2, 0x6B5EB0 x2, 0x6B5F30 x2, 0x6B5FC0 x2, 0x6B6040 x2, 0x6B60F0 x2, 0x6B6180, 0x6B61B0, 0x6B61E0, 0x6B6210, 0x6B6240, 0x6B6270, 0x6B62A0, 0x6B6410 x3, 0x6BC990, 0x6BFD10, 0x6BFD90, AutoTestManager__createSingleton, AutoTestManager__queueCommand, AutoTestManager__scalar_deleting_dtor x2, ClassCharacter__buildMeshTables x2 [A], ClassCharacterInstance__addClothBinding x2 [B], ClassCharacterInstance__releaseClothBindings [B], ClassCharacterTable__find [AB], ClassCharacterTable__insert x3 [A], ClassDynamicGeometry__buildMaterials x2, ClassDynamicGeometry__release x2, DebugMenuManager__createSingleton, DebugMenuManager__scalar_deleting_dtor, FFX_ChrLight_CreateSingleton x2, FFX_ChrLight_ctor, FFX_ChrLight_DestroySingleton, FFX_DebugPage_basicInfo_enable x9, FFX_MaterialText_Parse x4, RenderDelegationHelper_Add x3 [B], RenderDelegationHelper_RunFades x2, RenderDelegationHelper_RunPhase2 x2, RenderDelegationHelper_RunPhase4 x2, TextureAnimationInfo__scalar_deleting_dtor, TextureAnimationInfoChar__scalar_deleting_dtor, TextureAnimationManager__createSingleton x2, TextureAnimationManager__loadFromClusters x2 [AB], TextureAnimationManager__registerCharInstance x2 [B], TextureAnimationManager__scalar_deleting_dtor, TextureAnimationManager__unregisterCharInstance [B], ViewerManager__createSingleton, ViewerManager__scalar_deleting_dtor x3.

**FMOD glue** (138 sites, 44 functions, plus 0 sites in live code outside a function and 18 in orphan code)
: 0x701680, 0x702A80 x2, 0x702FA0, 0x70C3C0, 0x70CDE0 [B], 0x70D3A0 x3 [B], 0x70EDB0 x3 [B], 0x70FBF0 x3 [B], ??1TaskStack@details@Concurrency@@QAE@XZ_33, ??1TaskStack@details@Concurrency@@QAE@XZ_34, ??1TaskStack@details@Concurrency@@QAE@XZ_36, FfxFmod__ctor x6, FfxFmod__dtor x3, FfxFmod__shutdown [BF], FfxFmod_FileClose, FfxFmod_FileOpen x2, FmodManager__createSingleton x2, FmodManager__ctor x2, FmodManager__dtor, FmodManager__scalar_deleting_dtor, FmodMusic__ctor, FmodMusic__dtor x2, FmodMusic__setPs2Soundtrack x2 [B], FmodSfx__loadProject x2 [B], FmodSfx__playBattleStreaming x6 [B], FmodSfx__releaseAll x3 [BF], FmodSfx__start x12 [B], FmodSfx__stopAll x2 [BF], FmodSfx__tick x10 [F], FmodSfx__unloadProjectOf x2 [B], FmodSfx__unloadProjects x2 [BF], FmodVoice__ctor x5, FmodVoice__dtor x6, FmodVoice__initList x2 [F], JobWorker__vf00, SfxEvent__ctor x3 [B], SfxEvent__dtor x4 [BF], SfxEvent__fadePan x3 [B], SfxEvent__fadeVolume x3 [B], SfxEvent__start x2 [BF], SfxEvent__stop x2 [BF], SfxEvent__tick x2 [F], SfxProject__ctor x4 [B], SfxProject__unload x2 [BF].

**rcBg background** (136 sites, 77 functions, plus 1 sites in live code outside a function and 6 in orphan code)
: 0x925730, 0x9258F0 x4 [B], 0x934BC0, 0x935000 x3, 0x93E370 x2, camera_debug__f93E570 x3, movie_debug__f93DC00 x3, rcbgAnimeFinal [B], rcbgFPBltzInit, rcbgFPBltzNew, rcbgFPSlikInit, rcbgFPSlikNew, rcbgFPSsbtInit, rcbgFPSsbtNew, rcbgGuideBuildModel x2 [B], rcbgGuideDelete [B], rcbgGuideDrawFog x2, rcbgGuideNew [B], rcbgLoadReadModel [B], rcbgLoadReadScene [B], rcbgLoadReadSceneSet [B], rcbgLoadText__f93AE60, rcbgLoadText__f93B6B0, rcbgLoadTextScene x2, rcbgMainInit [B], rcbgMainTeardown [B], rcbgModelBuildBlob [B], rcbgModelDelete [B], rcbgModelFinal [B], rcbgModelNew [B], rcbgPreCreateDefaultSet x2 [B], rcbgPreFreeSet x3 [B], rcbgPreNewSet x2 [B], rcbgPSimCreate x4, rcbgPSimDelete, rcbgPSimNew, rcbgSceneFinal x3 [B], rcbgTrxAlloc, rcbgTrxDelete [B], rcbgTrxFinal [B], rcbgTrxNew [B], rcbgTrxTexDelete, rcbgTrxTexNew [B], rcdtChunkDelete [B], rcdtChunkFinal [B], rcdtChunkLoad, rcdtChunkNew, rcdtParseDelete, rcdtParseNew, rcdtRsdDelete, rcdtRsdFinal x5, rcdtRsdLoad x2, rcdtRsdLoad__LoadPly x3, rcdtRsdNew, rcdtTm2_LoadFile x2, rcdtTm2Delete, rcdtTm2Final x2, rcdtTm2New, rcfxDampDelete, rcfxDampNew, rcfxGravityDelete, rcfxGravityNew, rcfxNailDelete, rcfxNailFinal x4, rcfxNailNew, rcfxParticleClearForces, rcfxParticleDelete, rcfxParticleFinal x4, rcfxParticleNew, rcfxSpringDelete, rcfxSpringFinal x2, rcfxSpringNew, rcfxWindDelete, rcfxWindNew, rcuiPre__f930D80, rcuiPre__f932470 x2, SG_DebugWin_SplineEditorProc x14.

**battle and save data** (125 sites, 42 functions, plus 0 sites in live code outside a function and 3 in orphan code)
: 0x7803F0 [B], 0x780B40 x14, 0x782640 [B], 0x782750 [B], 0x783E00 [B], 0x787A40 [B], 0x78FB00, 0x7902F0, 0x790340 x3, 0x790430 x2, 0x793480 [B], 0x796DC0 [B], 0x797A90 x2 [B], 0x798F90 x2, 0x799EC0, 0x799F20 [B], 0x799F70 x2 [B], 0x79CCF0, 0x79DDE0 x2 [B], 0x79E630, 0x79E6A0 [B], 0x79F990 [B], 0x79F9B0 [B], FFX_Btl_AllocActorArrays x3 [B], FFX_Btl_AllocRewardRecords x2 [B], FFX_Btl_BeginBattle x2 [B], FFX_Btl_FreeActorArrays x2 [B], FFX_Btl_FreeActorMagicEffect, FFX_Btl_Init x8 [B], FFX_Btl_LoadFootData [B], FFX_Btl_LoadKernelTable x16 [B], FFX_Btl_LoadMonsterData [B], FFX_Btl_LoadWeaponModel [B], FFX_Btl_OpenFieldArchive x2, FFX_Btl_ReadKernelBin x2 [B], FFX_BtlSpecial_SetBlock x2 [B], FFX_InitNewSaveData x12 [B], FFX_KernelString_LoadGroup x14 [B], FFX_KeyItem_LoadTable x3 [B], FFX_SaveData__reloadCharNames x3, FFX_SaveData_GrantAllAbilities x2 [B], FFX_Treasure_LoadTables x2 [B].

**chr, motion, main step** (115 sites, 42 functions, plus 1 sites in live code outside a function and 10 in orphan code)
: 0x826340 x7, 0x831950 [B], 0x831BA0 [B], 0x83B5F0 [B], 0x83C710, 0x83D0A0 x2 [B], 0x83F050 [B], 0x83F8E0 x2, 0x83FF20 [B], FFX_Ch_AllocChrArray [B], FFX_Ch_AllocPartBoneBuffers x2 [B], FFX_Ch_BlkAllocate x2 [B], FFX_Ch_BuildJointOverrideMatrices, FFX_Ch_BuildSkeletonInstance x4 [B], FFX_Ch_CompactChrDataBlobs_dead x3, FFX_Ch_DataFree x10 [B], FFX_Ch_DebugSpawnByName, FFX_Ch_Dispose x10 [B], FFX_Ch_DisposeAll [B], FFX_Ch_FreePartBoneBuffers x5 [B], FFX_Ch_InitMemoryMoveCursor x2 [B], FFX_Ch_LoadAnmFile, FFX_Ch_LoadRomIndexTables [B], FFX_Ch_ModelCreatePacket x3 [B], FFX_Ch_ModelPurgePacket x6 [B], FFX_Ch_ReleaseResource [B], FFX_Ch_ReleaseTexListACopy [B], FFX_Ch_RomRead [B], FFX_Ch_RomReadEventMotionGroup, FFX_Ch_RomReadMotionSet [B], FFX_Ch_StepAll x2, FFX_ChCache_FreeAllOfType_dead x2, FFX_ChCache_FreeEntry x2 [B], FFX_ChCache_FreeInstanceTables x6 [B], FFX_ChCache_FreeSlot x2 [B], FFX_ChCache_FreeStaleUnlocked x2, FFX_ChCache_FreeUnlockedEntries x2 [B], FFX_ChCache_FreeZeroPriority x2 [B], FFX_MainStep, FFX_SgCapture_WriteBmp x6, FFX_SgMem_Init [B], Sg_PcRead [B].

**Bullet** (112 sites, 108 functions, plus 0 sites in live code outside a function and 4 in orphan code)
: 0x97C6F0, 0x97C750, 0x97C820, 0x97C850, 0x992080, 0x9DA078, 0x9DA098, _public__static_void___cdecl_btCollisionWorld_objectQuerySingle_class_btConvexShape_const___class_btTransform_const___class_btTransform_const___class_btCollisionObject___class_btCollisionShape_const___class_btTransform_const___struct_btCollisionWorld_ConvexResultCallback___float____vf00, _public__static_void___cdecl_btCollisionWorld_rayTestSingle_class_btTransform_const___class_btTransform_const___class_btCollisionObject___class_btCollisionShape_const___class_btTransform_const___struct_btCollisionWorld_RayResultCallback______vf00, _public__virtual_void___thiscall_btBvhTriangleMeshShape_processAllTriangles_class_btTriangleCallback___class_btVector3_const___class_btVector3_const___const___vf00, _public__virtual_void___thiscall_btHashedOverlappingPairCache_cleanProxyFromPairs_struct_btBroadphaseProxy___class_btDispatcher______vf00, _public__virtual_void___thiscall_btHashedOverlappingPairCache_removeOverlappingPairsContainingProxy_struct_btBroadphaseProxy___class_btDispatcher______vf00, _public__virtual_void___thiscall_btSortedOverlappingPairCache_cleanProxyFromPairs_struct_btBroadphaseProxy___class_btDispatcher______vf00, _public__virtual_void___thiscall_btSortedOverlappingPairCache_removeOverlappingPairsContainingProxy_struct_btBroadphaseProxy___class_btDispatcher______vf00, _public__virtual_void___thiscall_btTriangleMeshShape_processAllTriangles_class_btTriangleCallback___class_btVector3_const___class_btVector3_const___const___vf00, _public__void___thiscall_btOptimizedBvh_build_class_btStridingMeshInterface___bool_class_btVector3_const___class_btVector3_const______vf00, btActionInterface__vf00, btActivatingCollisionAlgorithm__vf00, btBoxBoxCollisionAlgorithm__vf00, btBoxBoxCollisionAlgorithm_CreateFunc__vf00, btBoxBoxDetector__vf00, btBridgedManifoldResult__vf00, btBroadphaseAabbCallback__vf00, btBroadphaseInterface__vf00, btBroadphaseRayCallback__vf00, btCharacterControllerInterface__vf00, btClosestNotMeConvexResultCallback__vf00, btCollisionAlgorithm__vf00, btCollisionAlgorithmCreateFunc__vf00, btCollisionConfiguration__vf00, btCollisionDispatcher__vf00, btCollisionPairCallback__vf00, btCollisionShape__vf00, btCollisionWorld__vf00, btCompoundCollisionAlgorithm__vf00, btCompoundCollisionAlgorithm_CreateFunc__vf00, btCompoundCollisionAlgorithm_SwappedCreateFunc__vf00, btConcaveShape__vf00, btConeTwistConstraint__vf00, btConstraintSolver__vf00, btContinuousConvexCollision__vf00, btConvexCast__vf00, btConvexConcaveCollisionAlgorithm__vf00, btConvexConcaveCollisionAlgorithm_CreateFunc__vf00, btConvexConcaveCollisionAlgorithm_SwappedCreateFunc__vf00, btConvexConvexAlgorithm__vf00, btConvexConvexAlgorithm_CreateFunc__vf00, btConvexPenetrationDepthSolver__vf00, btConvexPlaneCollisionAlgorithm__vf00, btConvexPlaneCollisionAlgorithm_CreateFunc__vf00, btConvexTriangleCallback__vf00, btDbvtBroadphase__vf00, btDefaultCollisionConfiguration__vf00, btDiscreteCollisionDetectorInterface__vf00, btDiscreteDynamicsWorld__vf00, btDispatcher__vf00, btDynamicsWorld__vf00, btEmptyAlgorithm__vf00, btEmptyAlgorithm_CreateFunc__vf00, btGeneric6DofConstraint__vf00, btGhostPairCallback__vf00, btGjkConvexCast__vf00, btGjkEpaPenetrationDepthSolver__vf00, btGjkPairDetector__vf00, btHashedOverlappingPairCache__vf00, btIDebugDraw__vf00, btInternalTriangleIndexCallback__vf00, btKinematicCharacterController__vf00, btKinematicClosestNotMeConvexResultCallback__vf00, btManifoldResult__vf00, btMinkowskiPenetrationDepthSolver__vf00, btNodeOverlapCallback__vf00, btNullPairCache__vf00, btOverlapCallback__vf00, btOverlappingPairCache__vf00, btOverlappingPairCallback__vf00, btPerturbedContactResult__vf00, btPointCollector__vf00, btSequentialImpulseConstraintSolver__vf00, btSimulationIslandManager__vf00, btSingleContactCallback__vf00, btSingleRayCallback__vf00, btSingleSweepCallback__vf00, btSortedOverlappingPairCache__vf00, btSphereSphereCollisionAlgorithm__vf00, btSphereSphereCollisionAlgorithm_CreateFunc__vf00, btSphereTriangleCollisionAlgorithm__vf00, btSphereTriangleCollisionAlgorithm_CreateFunc__vf00, btStaticPlaneShape__vf00, btStridingMeshInterface__vf00, btSubsimplexConvexCast__vf00, btTriangleCallback__vf00, btTriangleConvexcastCallback__vf00, btTriangleMeshShape__vf00, btTriangleRaycastCallback__vf00, btTypedConstraint__vf00, calcPenDepth_class_btVoronoiSimplexSolver___class_btConvexShape_const___class_btConvexShape_const___class_btTransform_const___class_btTransform_const___class_btVector3___class_btVector3___class_btVector3___class_btIDebugDraw___class_btStackAlloc______vf00, calculateAabbBruteForce_class_btVector3___class_btVector3______vf00, calculateTimeOfImpact_class_btCollisionObject___class_btCollisionObject___struct_btDispatcherInfo_const___class_btManifoldResult______vf00, CastResult__vf02, DebugDrawcallback__vf00, IslandCallback__vf00, performConvexcast_class_btTriangleCallback___class_btVector3_const___class_btVector3_const___class_btVector3_const___class_btVector3_const______vf00, performRaycast_class_btTriangleCallback___class_btVector3_const___class_btVector3_const______vf00, Result__vf00, solveConstraints_struct_btContactSolverInfo______vf00, SphereTriangleDetector__vf00, SupportVertexCallback__vf00.

**Phyre core and rendering** (72 sites, 55 functions, plus 0 sites in live code outside a function and 14 in orphan code)
: 0x4679D0 x2, 0x467A80, 0x505330, 0x50B940, 0x559960, 0x59F500 x3, Matrix4_____vf00, PAnimatableComponent_bool___vf00, PAnimatableComponent_bool_unsigned_int___vf00, PAnimatableComponent_float___vf00, PAnimatableComponent_float_unsigned_int___vf00, PAnimatableComponent_unsigned_int___vf00, PAnimatableComponent_void___vf00, PAnimatableComponent_void_float___vf00, PAnnotationWithValue_unsigned_int___vf00, PAttachableComponent_bool___vf00, PAttachableComponent_char_const_____vf00, PAttachableComponent_void___vf00, PAttachableComponent_void_bool___vf00, PFunctionCallerConcrete_double___vf00, PFunctionCallerConcrete_float___vf00, PInstancesComponent_unsigned_int___vf00, PLight_void_float___vf00, PLight_void_float_float___vf00, PLight_void_float_float_float_float___vf00, PLODGroup_void_bool___vf00, PMatrix4_const___unsigned_int___vf00, PMatrix4x3_const_____vf00, Point3___float___vf00, PResult_bool___vf00, PResult_float___vf00, PResult_unsigned_int_char_const_____vf00, PResult_unsigned_int_float_float___vf00, PResult_unsigned_int_unsigned_int_unsigned_int___vf00, PSpline_bool___vf00, PSpline_float___vf00, PSpline_float_unsigned_int___vf00, PSpline_unsigned_int___vf00, PSpriteAnimationInfo_unsigned_int___vf00, PSpriteAnimationInfo_void_char_const_____vf00, PSpriteAnimationInfoChar_void_char_const_____vf00, PSpriteAnimationInfoChar_void_float_float_float_float___vf00, PSpriteCollection_bool_unsigned_int___vf00, PSpriteCollection_int___vf00, PSpriteCollection_unsigned_int___vf00, PTimerComponent_void___vf00, PTrigger_bool___vf00, PTrigger_bool_char_const_____vf00, PTrigger_void_bool___vf00, PTypedObject_4__const_____vf00, PTypedObject_const___unsigned_int___vf00, Quat___float___vf00, Vector2___float___vf00, Vector3___float___vf00, Vector4___float___vf00.

**menu** (61 sites, 10 functions, plus 0 sites in live code outside a function and 2 in orphan code)
: 0x8ABA60 [B], FFX_Menu_Build x2, FFX_MenuSys_CreateTexLoad x2, FFX_MenuSys_ReleaseTexLoadWhenDone, FFX_MenuSysTexLoad_Advance x46, FFX_Path_RemapBattleHost0 [B], FFX_Path_RemapEventObjHost0 [B], sa_menu_start, sa_menu_stop x2, TOInit x2 [B].

**map, text, map fx** (57 sites, 17 functions, plus 15 sites in live code outside a function and 6 in orphan code)
: 0x90C400 x3 [B], 0x90CD90 [B], 0x90DD00 x2, 0x90E0B0 x4 [B], 0x90E2E0 x4 [B], 0x910A90 x4, 0x911AC0 x2, FFX_LoadEventIdTable [B], FFX_Map_LoadMapOutAsync [B], FFX_Map_LoadMapOutSync [B], FFX_Map_MountDevVpa, FFX_Map_UnloadMapOut [B], FFX_MapFx_CommitPointLights x2, FFX_MapFx_LoadParticleSet x2 [B], FFX_MapFx_ReleaseSet x2 [B], FFX_MapParticle_LoadPair x4, SG_DebugWin_FogParameterProc.

**ppp and vfx (yonishi)** (49 sites, 8 functions, plus 26 sites in live code outside a function and 6 in orphan code)
: 0x7108B0 [BF], 0x710960 [BF], 0x710BA0 [B], FFX_Vfx_PrimBuildInitBuckets [B], PppCore__f72C1B0 x5, pppDrawRain_BuildLines x2, SfxCommonTable__load [B], SfxLoopTable__load x5 [B].

**CRT startup and FFX_Config** (48 sites, 21 functions, plus 0 sites in live code outside a function and 6 in orphan code)
: 0x402D60 [A], 0x402DA0 [AB], 0x403010, 0x403830, 0x4038C0, 0x403920, 0x403EC0 x2, 0x404480 [A], 0x4045D0, 0x404740 x3 [AB], 0x404900 [AB], _Generic_error_category__vf00, _Iostream_error_category__vf00, _System_error_category__vf00, error_category__vf00, FFX_Config_ApplyKeyValue x3, FFX_Config_ParseFile x5, FFX_Config_Serialize x12, FFX_Config_SerializeBindings, std_basic_stringbuf_char_struct_std_char_traits_char__class_std_allocator_char____vf00, std_basic_stringbuf_char_struct_std_char_traits_char__class_std_allocator_char____vf03 x2.

**chr dispatch, sphere grid, menu build** (47 sites, 19 functions, plus 0 sites in live code outside a function and 11 in orphan code)
: 0xA40440 x4, 0xA41720, 0xA41750, 0xA417D0, 0xA41A10 x2, 0xA41D80, 0xA41FF0, 0xA4B4B0 x2, 0xA4B790 x2, 0xA54720 x2, CWindowEventHandler__onFocusLost x2, CWindowEventHandler__scalar_deleting_dtor x2, FFX_Asset_ReloadPathTable [B], FFX_SphereGrid_InitNodes [B], FFX_SphereGrid_LoadPanelTables x4 [B], FFX_SphereGrid_ReadPanelTables x4 [B], FFX_Str_ReplaceAllAlloc x3 [B], read_debug__fA42A00 [B], read_debug__fA42AB0 [B].

**chr cache and SG debug gui** (44 sites, 17 functions, plus 1 sites in live code outside a function and 1 in orphan code)
: 0x841440 [B], 0x8467B0 x2, 0x849BB0, 0x849BD0 x3, 0x849F40 x4, 0x84A710 x5, 0x84A810 x4, 0x84ABD0 x4, 0x84BAE0, 0x84F1C0, 0x8509E0 x6, 0x8531A0, 0x854E80, FFX_SG_DebugWin_LoadChrAnm, SG_DebugGui_OpenWindow, SG_DebugGui_RootWindowProc, SG_DebugWin_SetTitleRaw x5.

**VBF archives** (42 sites, 15 functions, plus 2 sites in live code outside a function and 6 in orphan code)
: 0x61ADD0 x10, 0x61B030 x3, 0x61B430, FFX_VbfArchive__open x4, FFX_VbfArchive__releaseStream [ABF], FFX_VbfManager__createSingleton x2, FFX_VbfManager__deletingDtor_ORPHAN, FFX_VbfManager__destroySingleton, FFX_VbfManager__mountArchive x3, FFX_VbfManager__setRootPath, FFX_VbfManager__unmountAll, FFX_VbfReqBlock__reset [ABF], FFX_VbfReqPool__init, FFX_VbfStream__ctor x3 [ABF], FFX_VbfStream__dtor [ABF].

**ATEL** (39 sites, 14 functions, plus 0 sites in live code outside a function and 7 in orphan code)
: FFX_Atel_PullActorPosFromChr, FFX_AtelSys_Btl_227_AllocScriptBlock_resi, FFX_AtelSys_Save_021_resi x2, FFX_AtelSys_Save_084_AllocActorPoolBlock_resi, FFX_AtelSys_Save_085_FreeTkBlock_resi, FFX_BtlEv_BuildScriptContext x2 [B], FFX_Ev_FreeAllActorPools, FFX_Ev_LoadEventPackage x5 [B], FFX_Ev_ReadEventPackageImage [B], FFX_EvDev_FreeDebugBuffers x3, FFX_Premovie_FreeBuffers x3, FFX_Premovie_ResetOnMapUnload x3 [B], maybe_FFX_Map_Unload x2 [B], maybe_FFX_TkHarness_Init_DEAD x6.

**magic effects (op, Oef2, resident)** (39 sites, 15 functions, plus 0 sites in live code outside a function and 22 in orphan code)
: 0x7E6C30 [B], 0x7F65D0, 0x7F7360, 0x7F73D0, 0x7F8930, 0x806AF0, 0x806B20, 0x806B70, 0x806BB0, 0x8070D0, 0x807120, 0x8072B0, 0x8075B0, FFX_Oef2_ExplodeModel x2, FFX_Rand_SeedCosmeticStreams x2 [B].

**async load** (31 sites, 16 functions)
: AsyncLoadManager__allocClusterRequest [AB], AsyncLoadManager__allocRequest [B], AsyncLoadManager__create x2, AsyncLoadManager__createChildList [AB], AsyncLoadManager__ctor x7, AsyncLoadManager__dtor, AsyncLoadManager__enqueue [B], AsyncLoadManager__scalar_deleting_dtor, AsyncLoadManager__workerThread x6 [A], FFX_AsyncLoad__charSubAssetJob [A], FFX_AsyncLoad_EnqueueTexListTaskA, LoadClusterRequest___vf00 x2, LoadClusterRequest__scalar_deleting_dtor, LoadClusterRequest__set x2 [AB], RegistTaskRequest___vf00 x2, RegistTaskRequest__scalar_deleting_dtor.

**movie lib and ATEL movie/save** (27 sites, 11 functions, plus 2 sites in live code outside a function and 0 in orphan code)
: 0x76DC70 [B], 0x7776A0 x4 [B], 0x778D70 x2 [B], 0x77A3F0, 0x77C6D0, 0x77F9C0 [B], FFX_SaMenu_HddInstallStart x2, hdd_thread_main x2, movie_str__f77C770 x2 [B], sa_task_createGroup [B], sceRead x8.

**Phyre PApplication and input** (25 sites, 7 functions, plus 0 sites in live code outside a function and 14 in orphan code)
: 0x625850, 0x625880 [B], 0x625960, 0x626B90, 0x6271D0, ?CheckStaticConstruction@SchedulerBase@details@Concurrency@@CAXXZ x2, _WinMain@16 x4.

**Phyre platform (D3D11, video)** (25 sites, 11 functions, plus 0 sites in live code outside a function and 9 in orphan code)
: 0xA25990, 0xA25EB0, 0xA26240, 0xA26270, 0xA288F0 x4, 0xA3FCD0, CDataFrameQueue__scalar_deleting_dtor, VbfMkvReader__Close [AB], VbfMkvReader__destroy [ABFI], VbfMkvReader__dtor x2, VbfMkvReader__Open x2 [B].

**mes win, pad, tklib, battle menu** (21 sites, 10 functions, plus 0 sites in live code outside a function and 1 in orphan code)
: 0x88CE80, 0x88D1D0, 0x88D240, FFX_LoadLocalizedBin [B], FFX_LoadLocalizedFtc [B], tklib__f88D710 x11 [B], tklib__f88DE60, tklib__f88DED0, FFX_Snd_ReloadMusicIndex, FFX_Snd_ReloadWaveIndex.

**FFX_Input and mem glue** (18 sites, 11 functions, plus 0 sites in live code outside a function and 3 in orphan code)
: 0x630460, 0x630540 [ABF], 0x630730, ??1TaskStack@details@Concurrency@@QAE@XZ, ??1TaskStack@details@Concurrency@@QAE@XZ_0, FFX_Input__dtor, FFX_Input__poll x2 [I], FFX_Input__reserveActionMap x3, FFX_MemTracker_Create x2, FFX_MemTracker_Forget [ABFI], FFX_MemTracker_Record [ABFI].

**Phyre physics and Bullet glue** (13 sites, 9 functions, plus 0 sites in live code outside a function and 1 in orphan code)
: 0x5F4230 x2, 0x5F56A0 x3, ClosestConvexResultCallback__vf00, ClosestRayResultCallback__vf00, ConvexResultCallback__vf00, Quat_const_____vf00, RayResultCallback__vf00, Vector3_const___float___vf00, Vector4_____vf00.

**SG editor and engine heap core** (12 sites, 6 functions, plus 0 sites in live code outside a function and 4 in orphan code)
: 0x945DE0, 0x9471D0, 0x94998E [ABF], ??2@YAPAXIABUnothrow_t@std@@@Z [ABF], ??_Etype_info@@UAEPAXI@Z x2, camera_debug__f942360 x2.

**Lua** (12 sites, 4 functions)
: ??1_Fac_tidy_reg_t@std@@QAE@XZ, ??_G_Fac_node@std@@QAEPAXI@Z, ?_Facet_Register@std@@YAXPAV_Facet_base@1@@Z, libmscd__f76C560 x9 [B].

**Phyre async and fios** (10 sites, 2 functions, plus 0 sites in live code outside a function and 6 in orphan code)
: 0x611880, 0x611F70 x3.

**SG debug windows** (8 sites, 4 functions, plus 1 sites in live code outside a function and 2 in orphan code)
: 0x7CF8F0, 0x7CF970, SG_DebugWin_CameraFilterProc, SG_DebugWin_CameraProc x2.

**Iggy glue, vorbis, CRT** (7 sites, 7 functions)
: 0xB06050, 0xB066E0, 0xB067F0, 0xB06CB0, 0xB07920, 0xB07C20, 0xB07CA0.

**ATEL btl and battle AI** (5 sites, 3 functions)
: 0x7A0CB0, 0x7AD820, FFX_Equip_LoadNameTable x3 [B].

**FMV** (5 sites, 3 functions)
: FFX_Fmv_Manager_Construct x2, FFX_Fmv_StepPresentation x2, FFX_Fmv_UploadFrameJob.

**ATEL ch libs and ev script** (4 sites, 3 functions)
: 0xA6A6A0, 0xA6D890, 0xA6ED40 x2.

**MagicFile** (2 sites, 1 functions)
: MagicFile__unload x2.

**FFXApplication** (2 sites, 1 functions)
: FFX_GetFmvState x2.

**Phyre scripting** (2 sites, 1 functions, plus 0 sites in live code outside a function and 1 in orphan code)
: PTypedObject_char_const___unsigned_int___vf00.

**other** (2 sites, 0 functions, plus 0 sites in live code outside a function and 2 in orphan code)
: .

Phyre layer, callers inside Phyre and the CRT static init region (counted, not listed): 4715 sites, 2823 functions.

**Phyre core and rendering** (2165 sites, 856 functions, plus 136 sites in live code outside a function and 746 in orphan code)
.

**Phyre (by name)** (1566 sites, 1435 functions)
.

**Phyre platform (D3D11, video)** (349 sites, 127 functions, plus 14 sites in live code outside a function and 95 in orphan code)
.

**Iggy glue, vorbis, CRT** (294 sites, 294 functions)
.

**Phyre scripting** (140 sites, 47 functions, plus 20 sites in live code outside a function and 47 in orphan code)
.

**Phyre async and fios** (81 sites, 31 functions, plus 4 sites in live code outside a function and 28 in orphan code)
.

**Phyre physics and Bullet glue** (74 sites, 21 functions, plus 0 sites in live code outside a function and 33 in orphan code)
.

**Phyre PApplication and input** (34 sites, 12 functions, plus 2 sites in live code outside a function and 13 in orphan code)
.

**other** (12 sites, 0 functions, plus 12 sites in live code outside a function and 0 in orphan code)
.

Phyre layer, game side callers (listed): 809 sites, 284 functions.

**FFX PC layer (boot, fmv, save, chr cluster, async load)** (323 sites, 116 functions, plus 1 sites in live code outside a function and 72 in orphan code)
: 0x634190 x2, 0x634700 x3 [ABFI], 0x634790 x4 [ABFI], 0x634850 x25 [ABFI], 0x6353F0 x3, 0x635630 [B], 0x636970 x3 [ABFI], 0x637E30 [B], 0x637EC0 [B], 0x63A610 x2, 0x63D710, 0x640100 [B], 0x641E60, 0x643340 [B], 0x64B8C0 x2 [AB], 0x64CB00 [AB], 0x64CBA0 [AB], 0x64F7F0, 0x64F860, 0x6567E0, 0x65A2C0, 0x65A3A0, 0x65FAC0 [AB], 0x65FB50 [B], 0x65FBE0, 0x66B560, 0x66C240 x2 [B], 0x66C690, 0x66C700, 0x66FB70 x3 [B], 0x670760 x3 [B], 0x672760, 0x6736E0, 0x6740B0, 0x674740 x4 [A], 0x674CE0, 0x675600, 0x676030, 0x676680 x4 [A], 0x676C60, 0x677690, 0x677CE0 x4, 0x678530, 0x678BD0 x4 [A], 0x67AD30 x10 [A], 0x67B230 x8, 0x67C440 x8 [A], 0x67FF10 x2, 0x689620 x3, 0x68A660 x4, 0x68B860, 0x68B910, 0x68BA20, 0x68BAE0 x2, 0x68BBA0, 0x68BC60 x2, 0x692950, 0x6931A0 x3 [A], 0x693D70 x9 [A], 0x6991F0 [AB], 0x699B70 [AB], 0x69A130 x2 [AB], 0x69A6B0 x6 [B], 0x69A9C0 x3, 0x69AC10 x5, 0x69B310 [B], 0x69B530, 0x69B5C0 [B], 0x69B650 [A], 0x69BB80 x3 [AB], 0x69BDB0 [ABFI], 0x69BE90 [B], 0x69E140 x2, 0x69E900 x3, 0x69F900 x3 [B], BrokenScreenPolygonDynamicMesh_ClassDescAbstract__vf07, BrokenScreenPolygonDynamicMesh_ClassDescConcrete__vf07, BrokenScreenPolygonDynamicMesh_ClassDescForType__vf07, BrokenScreenPolygonDynamicMeshInstance_ClassDescAbstract__vf07, BrokenScreenPolygonDynamicMeshInstance_ClassDescForType__vf07, BrokenScreenPolygonDynamicMeshInstance_ClassDescWithoutDefaultConstructor__vf07, ClassCharacter__createInstance x2 [B], ClassCharacter__unload x2 [B], ClassDynamicMesh__setupStreams x2, ClassDynamicMesh_ClassDescAbstract__vf07, ClassDynamicMesh_ClassDescConcrete__vf07, ClassDynamicMesh_ClassDescForType__vf07, ClassDynamicMeshInstance_ClassDescAbstract__vf07, ClassDynamicMeshInstance_ClassDescForType__vf07, ClassDynamicMeshInstance_ClassDescWithoutDefaultConstructor__vf07, DistortionGridDynamicMesh_ClassDescAbstract__vf07, DistortionGridDynamicMesh_ClassDescConcrete__vf07, DistortionGridDynamicMesh_ClassDescForType__vf07, DistortionGridDynamicMeshInstance_ClassDescAbstract__vf07, DistortionGridDynamicMeshInstance_ClassDescForType__vf07, DistortionGridDynamicMeshInstance_ClassDescWithoutDefaultConstructor__vf07, FFX_Chr_AttachModelInstance [B], FFX_Chr_CancelPendingAttachments [B], FFX_Chr_ProcessPendingAttachments, FFX_Chr_ReleaseInstance x2 [B], FFX_Gfx_ApplyGlobalCameraAmbient x2 [B], FFX_Gfx_ClearModelMatrixOverrideList, FFX_Gfx_FinishMapLoad x10 [B], FFX_Gfx_PushModelMatrixOverride, FFX_Gfx_ResetMapSceneState [B], FFX_GfxCtx_ctor x7, FFX_TexList_LoadAndEnqueue x2 [AB], RadialLineDynamicMesh_ClassDescAbstract__vf07, RadialLineDynamicMesh_ClassDescConcrete__vf07, RadialLineDynamicMesh_ClassDescForType__vf07, RadialLineDynamicMeshInstance_ClassDescAbstract__vf07, RadialLineDynamicMeshInstance_ClassDescForType__vf07, RadialLineDynamicMeshInstance_ClassDescWithoutDefaultConstructor__vf07, ShadowDynamicMesh_ClassDescAbstract__vf07, ShadowDynamicMesh_ClassDescConcrete__vf07, ShadowDynamicMesh_ClassDescForType__vf07.

**FFX debug managers** (243 sites, 66 functions, plus 2 sites in live code outside a function and 78 in orphan code)
: 0x6A25E0 [AB], 0x6A2670 [B], 0x6A2700 [B], 0x6A2790 [B], 0x6A94B0 [A], 0x6AB540, 0x6AB8C0 [B], 0x6AB950 x2 [B], 0x6ABC20 x5, 0x6ABDB0, 0x6AC1B0 x8 [A], 0x6AC380 [B], 0x6AC420, 0x6AC530 x8 [B], 0x6ACBD0 x4, 0x6AD0D0, 0x6AD180, 0x6ADC90 x2, 0x6ADD90 [B], 0x6AE730 x6 [AB], 0x6AE9C0 x2 [AB], 0x6AEB20 [AB], 0x6AEEE0 x3 [AB], 0x6AF0D0 [AB], 0x6AF1C0, 0x6AF270 [AB], 0x6AF510, 0x6AF5D0, 0x6AF630 [AB], 0x6AF690 x4 [AB], 0x6AF760 x2 [AB], 0x6AF7F0 x3 [AB], 0x6AF8A0 x4, 0x6AFA70 [AB], 0x6AFAF0 x2 [AB], 0x6AFFC0 [AB], 0x6B0010 [AB], 0x6B01F0 [AB], 0x6B0230 [AB], 0x6B03A0, 0x6B1B60 [B], 0x6B2400 x3 [AB], 0x6B24E0 [AB], 0x6B2570 [AB], 0x6B26B0, 0x6B3E10, 0x6B3FE0 x9 [B], 0x6BD9A0 x7, 0x6BDE30 x2, 0x6BEE80 [B], 0x6BF170 [B], 0x6BF360 x2, 0x6BF9C0 x2, 0x6BFD10 x2, ClassCharacterInstance__addClothBinding [B], ClassDynamicGeometry__vf05 x3, FFX_ChrLight_Initialize x2 [A], FFX_ChrLight_RegisterMeshInstance x2 [B], FFX_ChrLight_UnregisterMeshInstance [B], FFX_MaterialText_Parse x2, RenderDelegationHelper_Add [B], TextureAnimationInfo__scalar_deleting_dtor x2, TextureAnimationManager__clearMap x3 [B], TextureAnimationManager__loadFromClusters x25 [AB], TextureAnimationManager__registerCharInstance x4 [B], TextureAnimationManager__unregisterCharInstance x2 [B].

**FFX PC layer (fmv, esc menu, cloth, booster, dll, mem)** (211 sites, 94 functions, plus 1 sites in live code outside a function and 39 in orphan code)
: 0x6C04E0 [B], 0x6C0520, 0x6C0890, 0x6C0980, 0x6C1520 x4, 0x6C19B0 x6 [B], 0x6C1C50 x6, 0x6C2410 x2, 0x6C2520 [A], 0x6C2710 x3 [A], 0x6C2FF0 x2 [A], 0x6C31C0 x3, 0x6C32D0, 0x6C35D0 [A], 0x6C3CD0, 0x6C40E0, 0x6C4B60, 0x6C5150 [A], 0x6C5EC0, 0x6C6390, 0x6C6830, 0x6C6C30, 0x6C7E60, 0x6C8090 x4, 0x6C9580 x2 [A], 0x6C9E50 x3, 0x6CAEE0 x2, 0x6D17D0, 0x6D1B40 x3 [A], 0x6D26D0, 0x6D2760, 0x6D4780 x2, 0x6D4BA0 x2, 0x6D51D0 x2 [A], 0x6D5A00 x3, 0x6D6D00 x2, 0x6D7FB0 x2, 0x6DC8D0 x2, 0x6DCA70, 0x6DD230 x2, 0x6DD5A0 x4 [B], 0x6DDBF0, 0x6DDD60, 0x6DDFA0, 0x6DE2C0 x2, 0x6DE420 x2, 0x6DE720, 0x6DEA60, 0x6DF6D0, 0x6DFAD0 x2, 0x6E2760, 0x6E27E0, 0x6E2860, 0x6E37D0 x2, 0x6E45E0, 0x6E4770 x2, 0x6E4920 x4, 0x6E5020 [ABFI], 0x6E5EE0, 0x6E7940 x3 [A], 0x6E81F0, 0x6F5C40 x2 [A], 0x6F5E30 x5 [A], 0x6F60A0 x4 [A], 0x6F6950 x2, 0x6F7C10 [A], 0x6F8C50 x2 [A], 0x6F9580, 0x6FBC10, 0x6FBCD0, 0x6FC0A0 x2, Cloth_InstallInstancingMesh x4 [B], ClothInstancing_QueueDeferredRelease x2 [B], ClothInstancingDynamicMesh__buildStreamDescs x10 [B], ClothInstancingDynamicMesh__vf00, ClothInstancingDynamicMesh_ClassDescAbstract__vf07, ClothInstancingDynamicMesh_ClassDescConcrete__vf07, ClothInstancingDynamicMesh_ClassDescForType__vf07, ClothInstancingDynamicMeshInstance__vf00, ClothInstancingDynamicMeshInstance_ClassDescAbstract__vf07, ClothInstancingDynamicMeshInstance_ClassDescForType__vf07, ClothInstancingDynamicMeshInstance_ClassDescWithoutDefaultConstructor__vf07, DynamicMeshDefaultImplmenetation__vf00, DynamicMeshDefaultImplmenetation_ClassDescAbstract__vf07, DynamicMeshDefaultImplmenetation_ClassDescConcrete__vf07, DynamicMeshDefaultImplmenetation_ClassDescForType__vf07, FFX_Chr_ApplyLevelDepthBias [B], FFX_MemReport_CreateSingleton, FFX_MemReport_Destroy, FFX_Vfx_LoadShaderManifest x2 [A], FFX_VfxShaderTable_Find x3, ShadowDynamicMeshInstance_ClassDescAbstract__vf07, ShadowDynamicMeshInstance_ClassDescForType__vf07, ShadowDynamicMeshInstance_ClassDescWithoutDefaultConstructor__vf07.

**chr dispatch, sphere grid, menu build** (15 sites, 1 functions, plus 0 sites in live code outside a function and 5 in orphan code)
: 0xA40440 x10.

**FFXApplication** (5 sites, 4 functions, plus 0 sites in live code outside a function and 1 in orphan code)
: 0x42F090 [AB], 0x42F220, FFXApplication__dtor, j_PhyreFree [AB].

**VBF archives** (4 sites, 0 functions, plus 0 sites in live code outside a function and 4 in orphan code)
: .

**FMOD glue** (3 sites, 1 functions, plus 0 sites in live code outside a function and 2 in orphan code)
: 0x703230 [A].

**FMV** (2 sites, 2 functions)
: FFX_Fmv_BindVideoMaterial [A], FFX_Fmv_DestroyPlayer [B].

**ppp and vfx (yonishi)** (2 sites, 0 functions, plus 0 sites in live code outside a function and 2 in orphan code)
: .

**FFX_Input and mem glue** (1 sites, 0 functions, plus 0 sites in live code outside a function and 1 in orphan code)
: .

CRT heap: 388 sites, 93 functions.

**Iggy glue, vorbis, CRT** (227 sites, 54 functions, plus 8 sites in live code outside a function and 23 in orphan code)
: 0xA8F7F0 [B], 0xA8FAC0 x4 [B], 0xA90390 x3, 0xA90540 x2, 0xA916C0 x4, 0xA91B30 x11 [B], 0xA920E0 x3 [AB], 0xA92150 x2, 0xA926F0 [B], 0xA92A60 x2 [B], 0xA92D50 x6 [B], 0xA93550 x3 [B], 0xA935B0 x5 [B], 0xA946E0 x7 [B], 0xA96840 x3 [B], 0xA97330 x8 [AB], 0xA974B0 x5 [B], 0xA97DD0 [B], 0xA9B190 x2 [B], 0xA9B7A0 x6 [AB], 0xA9B840 x4, 0xA9BB70, 0xA9D640 x2 [AB], 0xA9D680 x2 [B], 0xA9EA70 x2, 0xA9EB80 x4, 0xA9EDF0 x4, 0xA9F130 x2, 0xA9FEA0 x5, 0xAA00F0 x2, 0xAA03D0 x5, 0xAA0450, 0xAA0790 x16, 0xAA15F0 x2, 0xAA19A0, 0xAA2700, 0xAA2A00 x2, 0xAA2F40, 0xAA2F70, 0xAA2FA0 x2, 0xAA3E50 x2, 0xAA3EB0, 0xAA4090 x2, 0xAA4190 x4, 0xAA4200, 0xAE0400, 0xAE0410, 0xB07DBA, vorbis_block_clear x4 [AB], vorbis_block_init x3 [B], vorbis_comment_clear x4 [AB], vorbis_dsp_clear x20 [AB], vorbis_info_clear x12 [AB], vorbis_info_init [B].

**SG debug windows** (52 sites, 9 functions, plus 8 sites in live code outside a function and 16 in orphan code)
: 0x7CA4A0 x2, 0x7CA580 x4, 0x7CB640 x4, 0x7CD5A0 x2, 0x7CD800 x4, 0x7CE910 x2, 0x7CF780 x2, 0x7DD480 x4, 0x7DD5E0 x4.

**FFX PC layer (boot, fmv, save, chr cluster, async load)** (34 sites, 7 functions, plus 0 sites in live code outside a function and 6 in orphan code)
: 0x6496D0 x6 [B], 0x66FB70 x4 [B], 0x687210 x4, 0x687370 x4, 0x687C10 x4, 0x687E90 x4, FFX_LoadDllOrFatal x2.

**FFXApplication** (14 sites, 5 functions, plus 0 sites in live code outside a function and 3 in orphan code)
: 0x42E480 x2, 0x42E5F0 x2, 0x42E780, FFX_Ogg_ResetAllocators x4 [AB], oggpack_write x2.

**libvpx** (12 sites, 2 functions)
: 0x40DB70 x6, 0x41B0B0 x6.

**magic effects (op, Oef2, resident)** (12 sites, 3 functions, plus 0 sites in live code outside a function and 4 in orphan code)
: 0x7E1010 x4, 0x7E2270 x2, 0x7E2ED0 x2.

**FFX PC layer (fmv, esc menu, cloth, booster, dll, mem)** (8 sites, 2 functions, plus 2 sites in live code outside a function and 0 in orphan code)
: FFX_Heap_Create x4 [ABFIR], maybe_FFX_FmvMgr_PlayFromJob x2.

**other** (6 sites, 0 functions, plus 6 sites in live code outside a function and 0 in orphan code)
: .

**Bullet** (5 sites, 3 functions, plus 0 sites in live code outside a function and 2 in orphan code)
: 0x9D9D0A, btAlignedAllocDefault, btAlignedFreeDefault.

**CRT startup and FFX_Config** (4 sites, 2 functions)
: 0x408DF0 x2, 0x408E10 x2.

**Lua** (4 sites, 1 functions)
: lauxlib_l_alloc x4.

**mkvparser and ogg** (2 sites, 1 functions)
: FFX_Mkv_ResetAllocators x2 [AB].

**Phyre (by name)** (2 sites, 1 functions)
: Phyre_AppendToPathEnv x2.

**FFX_Input and mem glue** (2 sites, 1 functions)
: 0x630460 x2.

**FMV** (2 sites, 1 functions)
: FFX_FmvMgr_PlayPath x2.

**battle and save data** (2 sites, 1 functions)
: FFX_Btl_LoadBtlEnvFile x2.

ppp heaps: 79 sites, 43 functions.

**ppp and vfx (yonishi)** (55 sites, 26 functions)
: 0x712560, FFX_Ppp_BirthParticle x2, FFX_Ppp_FreeParticle x2 [B], FFX_Ppp_StartPart, maybe_FFX_Vfx_FreePrimList [B], pppDrawMdlInf__f73C8E0 x3, pppDrawMdlInf_Des x3, pppDrawMdlPSim_Create x2, pppDrawMdlSea__f73B5C0 x2, pppDrawMdlSea_Des x2, pppDrawRain_CreateDrops, pppDrawRain_Des, pppDrawShapeCamera__f747760 x2, pppDrawShapeCamera_Des x2, pppDrawShapeCameraDisPos__f7489B0 x2, pppDrawShapeCameraDisPos_Des x2, pppDrawShapeField__f742F20 x3, pppDrawShapeField_Des x3, pppDrawShapeFieldGlobal__f746540 x2, pppDrawShapeFieldGlobal_Des x2, pppDrawShapeFieldRev__f744260 x3, pppDrawShapeFieldRev_Des x3, pppDrawShapeFieldSpd__f7454A0 x4, pppDrawShapeFieldSpd_Des x4, pppVtMime_Des, pppVtMime_Draw.

**rcBg background** (10 sites, 8 functions, plus 0 sites in live code outside a function and 1 in orphan code)
: 0x9251C0, 0x92ED50, 0x930030, 0x930550, 0x930600, 0x930710, 0x930800, FFX_Yanagi_InitProgram x2.

**chr dispatch, sphere grid, menu build** (4 sites, 4 functions)
: FFX_SphereGrid_ActivateNodeForChar, FFX_SphereGrid_BeginNodeKindChange, FFX_SphereGrid_BeginWarp, FFX_SphereGrid_StepWarp.

**movie lib and ATEL movie/save** (3 sites, 2 functions, plus 0 sites in live code outside a function and 1 in orphan code)
: 0x760610, 0x761D60 [B].

**other** (3 sites, 0 functions, plus 0 sites in live code outside a function and 3 in orphan code)
: .

**map, text, map fx** (2 sites, 1 functions)
: FFX_MapFx_LoadParticleSet x2 [B].

**magic effects (op, Oef2, resident)** (1 sites, 1 functions)
: FFX_MagicResident_OvlInit.

**menu** (1 sites, 1 functions)
: FFX_Menu_Build.

Oef2 object heap (op_malloc): 33 sites, 18 functions.

**magic effects (op, Oef2, resident)** (22 sites, 13 functions)
: 0x8014C0 [B], 0x8061A0 [B], FFX_Oef2_Op033, FFX_Oef2_Op040 x5, FFX_Oef2_Op074, FFX_Oef2_Op078, FFX_Oef2_Op111 x2, FFX_Oef2_Op132 x3, FFX_Oef2_Op133, FFX_Oef2_Op134 x2, FFX_Oef2_Op137, FFX_Oef2_Op143, FFX_Oef2_Op168 x2.

**ATEL ch libs and ev script** (11 sites, 5 functions)
: 0xA6DBE0 x2, 0xA70910 x3, 0xA74410, 0xA74C60, 0xA74D30 x4.

menu object pool: 40 sites, 40 functions.

**menu** (40 sites, 40 functions)
: 0x8A8E70, 0x8B3C20, 0x8B3C70, 0x8B4FB0, 0x8B5000, 0x8C43F0, 0x8C4450, 0x8C9140, 0x8C9240, 0x8C9330, 0x8CD140, 0x8CD1B0, 0x8CED30, 0x8CEDA0, 0x8CFC90, 0x8D6CE0, 0x8D81D0, 0x8D8260, 0x8D82D0, 0x8D8370, 0x8D83E0, 0x8DA860, 0x8DA8F0, 0x8DA950, 0x8DA9C0, 0x8DAA00, 0x8E33D0, 0x8E3470, 0x8E3510, 0x8E35B0, 0x8E45E0, 0x8E4790, FFX_Menu_BuildMainMenuRows, FFX_Menu_CustomizeCreateListPanel, FFX_Menu_Od_CreateAbilityPanel, FFX_Menu_Od_CreateModePanel, FFX_Menu_StatusCreateListPanel, FFX_Menu_StatusCreateMainWindow, FFX_Menu_WorldMapCreateWindow, FFX_NameInput_Open.

prim build buckets: 9 sites, 8 functions.

**ppp and vfx (yonishi)** (5 sites, 4 functions)
: 0x718610, 0x71B8A0 x2, 0x71C0B0, pppKeMdlDtt__f72DED0.

**ATEL ch libs and ev script** (3 sites, 3 functions)
: 0xA6A6A0, 0xA6E2E0, 0xA6E650.

**chr dispatch, sphere grid, menu build** (1 sites, 1 functions)
: 0xA5C370.

FMOD callbacks: 3 sites, 1 functions.

**FMOD glue** (3 sites, 1 functions)
: FfxFmod__init x3 [B].

Lua allocator: 3 sites, 2 functions.

**Phyre core and rendering** (2 sites, 1 functions, plus 1 sites in live code outside a function and 0 in orphan code)
: 0x554860.

**Phyre (by name)** (1 sites, 1 functions)
: PScripting_RunStandaloneScriptFile.
