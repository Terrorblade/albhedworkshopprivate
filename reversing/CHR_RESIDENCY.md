# CHR residency: can a plugin make an arbitrary model loadable at any moment?

## Verdict

**Yes, and it needs exactly one call.** `FFX_Ch_LoadChrData` RVA `0x00425A40` already contains the
whole residency sequence, and `FFX_Ch_Allocate` RVA `0x00424F90` calls it, so a plugin that just
calls `FFX_Ch_Allocate(chrId)` on the game thread gets any of the 892 ROM-indexed models paged in
and bound in the same frame. The reason the existing mod code gave up is a wrong premise on both
halves. First, `FFX_Ch_RomRead` RVA `0x00429EF0` is not a lookup, it *creates* the cache entry and
starts the read, so `FFX_Ch_DataReadSync` returning 2 is a statement about a call that has not been
made yet, not about a model that cannot be loaded. Second, there is a blocking pump,
`FFX_Ch_RomPump` RVA `0x0042A5E0`, whose argument Hex-Rays loses because the function is a tail
jump, and `FFX_Ch_LoadChrData` calls it with 0, which blocks the calling thread until the read has
finished and the completion callback has run. And third, the "error box" does not exist in retail,
`yiAssert` is a `vprintf` to stdout with every draw and abort path stubbed to a bare `retn`, so even
the DataReadSync route is recoverable. The real limits are elsewhere and they are hard ones: there
are only **40 CHRDATA records**, the table full is a null-pointer `memset` and not a failed
allocation, and the swap still does not survive a map change for the reasons already written up in
`CHEAT_MODELS.md` section 3.

Addresses here are **RVAs**, which is the IDA VA minus `0x400000`, matching `CHEAT_MODELS.md`.

---

## 1. What the cache is, and what fills it

### The cache itself

`FFX_Ch_DataReadSync` is a thin wrapper over `FFX_ChCache_GetState(1, chrId)` RVA `0x0043FFE0`, and
that function is a linear scan of one array:

```
g_ffxChrCacheSlots     RVA 0x00F02F98   void *[100]
g_ffxChrCacheSlotsEnd  RVA 0x00F03128   the end address, not a variable
```

The 100 is not inferred. The raw loop bound is `cmp eax, offset g_ffxChrCacheSlotsEnd`, an immediate
operand, so `(0xF03128 - 0xF02F98) / 4 = 100` is fixed at compile time. Each element is a pointer to
a record the engine's log strings call a "Memory Cache" entry:

| offset | meaning |
|---|---|
| +0 | key. For CHRDATA it is the chrId. For a motion set it is `modelId \| (mode << 16)` |
| +4 | type byte. 1 = CHRDATA, 2 = motion set, 3 and 4 exist in the eviction logger |
| +5 | lock. 1 means `FFX_ChCache_EvictOne` must not touch it. A boolean, not a refcount |
| +6 | read in flight. 1 while the file read is outstanding, 0 once it has landed |
| +7 | spare flag |
| +8 | low 3 bits are the eviction priority. 7 means never evict. `CreateEntry` starts it at 5 |
| +12 | the raw blob. Non-zero is what "this slot is occupied" means everywhere |
| +16 | primary `ClassCharacter *`, the Phyre mesh. What `FFX_ChCache_GetModel` returns |
| +20 | secondary `ClassCharacter *` |
| +24, +28 | the `.cdf` and `.cmf` handles, filled a frame or two later |

`FFX_ChCache_GetState` returns, in order of the checks it performs:

- **2** when no record matches `(type, key)` *with a non-null blob at +12*. That is all. It does not
  mean the file is missing and it does not mean the model is unloadable.
- **1** when a record matches and +6 is non-zero, the read is still in flight.
- **0** when a record matches and +6 is zero, the data is there.

### What populates it, and when

Three producers, all of them going through `FFX_ChCache_CreateEntry` RVA `0x00440560`:

1. **`FFX_Ch_RomRead` RVA `0x00429EF0`**, for CHRDATA, type 1.
2. **`FFX_Ch_RomReadMotionSet` RVA `0x0042A4C0`**, for a model's `resident<mode>.mgrp`, type 2.
3. **`FFX_Mot_BeginSystemMgrpRead` RVA `0x004376B0`**, for the shared system motion group.

So the only thing that ever makes a chrId resident is `FFX_Ch_RomRead`, and the question of "what is
resident on a map" reduces to "who called `FFX_Ch_RomRead`". There are three callers that matter:

- **`FFX_Field_ChrRegTask` RVA `0x00461850`, the `"CHRREG"` boot task.** This is the per-map
  populate. It walks every ATEL actor of the loaded event package and runs a state machine per
  actor: case 2 does `FFX_Ch_RomRead(actor+168)` for the body and case 3 polls
  `FFX_Ch_DataReadSync(...) == 1`, case 4/5 does the same for the weapon id at `actor+2820`, case 6/7
  for the armour id at `actor+2822`, case 8/9 and 10/11 start and poll motion modes 0 and 2, and
  case 12 finally spawns the CHR. **The resident set on a map is therefore exactly the chr ids
  authored into that package's actors, and nothing more.** It is per event package, not per area,
  and there is no global preload.
- **`FFX_AtelSys_Ch_078_resi` RVA `0x00679800`**, the script-visible residency command. Its whole
  body is `FFX_Ch_RomRead(FFX_Atel_PopInt(...))`. Its poll partners
  `FFX_AtelSys_Ch_079_poll 0x00679380` and `FFX_AtelSys_Ch_100_poll 0x00679290` wrap
  `FFX_Ch_DataReadSync`. So an event package declares the extra models a cutscene needs and waits.
- **`FFX_Ch_LoadChrData` RVA `0x00425A40`**, on demand, which is the one that matters for a plugin.

### What empties it

- **`FFX_Ch_EventjumpDispose` RVA `0x0043B420`** on every map transition: `FFX_Ch_DisposeAll` then
  `FFX_Ch_DataDisposeAll` RVA `0x004259C0`, which frees all 40 CHRDATA records, and each
  `FFX_Ch_DataFree` RVA `0x004258D0` calls `FFX_ChCache_Unlock(1, id)`.
- **`FFX_ChCache_Unlock` RVA `0x00440D70`** clears +5 and sets bit `0x40` of +8. It does **not** free
  anything, it only makes the entry eligible for reclaim.
- **`FFX_ChCache_EvictOne` RVA `0x004409A0`** reclaims one unlocked, not-loading entry, picking the
  lowest priority then the lowest blob address. Called only from `FFX_ChCache_CreateEntry` when all
  100 slots are taken.
- **`FFX_ChCache_FreeUnlockedEntries` RVA `0x00440030`** frees every unlocked entry in one pass.
  Reached from the SG debug GUI and from `FFX_AtelSys_Sg_031_resi`, plus the map load/unload paths
  through `0x00509EA0`.

The practical consequence: a model a plugin loads is locked by `FFX_Ch_LoadChrData`, so it cannot be
evicted out from under a live CHR, and it is released on the next map transition. No permanent leak,
no manual cleanup needed.

---

## 2. `FFX_Ch_RomRead` traced to the bottom

It enqueues, and there is both a blocking and a deferred way to wait. Full body, from the
disassembly at `0x00829EF0` (VA):

```c
int __cdecl FFX_Ch_RomRead(int chrId)
{
    if ((unsigned)FFX_ChCache_GetState(1, chrId) < 2)
        return 0;                               // raw: cmp eax,1/jz then test eax,eax/jz
    int rom = FFX_Ch_FindRomEntry(chrId);       // also SELECTS the asset kind, see below
    if (rom < 0)
        return -1;                              // not in g_ffxChrRomIndexTables
    int size = g_ffxRomDevGetSize(rom);
    if (size == 0)
        return -1;                              // the file does not ship
    void* blob = FFX_MemAlloc(((size + 0x8F) & ~0x7F) - 0x10);
    const char* path = g_ffxRomDevGetPath(rom);
    FFX_ChCache_CreateEntry(1, chrId, blob);
    FFX_ChCache_SetLoading(1, chrId, 1);
    // ctx is a 16-byte STACK local at ebp-0x60 holding { .., chrId@+12, blob@+16, 1@+20 }
    g_ffxAssetLoader(rom, blob, FFX_Ch_OnRomReadComplete, &ctx);
    FFX_ChCache_GetModelSlots(1, chrId, &pModel, &pModel2, &pCdf, &pCmf);
    *pModel  = FFX_Ch_LoadModel(path, pCdf, pCmf, chrId);
    *pModel2 = 0;
    return 0;
}
```

**Return sense: 0 means accepted or already present, -1 means refused.** The early `return 0` for
state 0 or 1 is what makes the call idempotent.

The stack-local ctx is safe, and that is worth stating because it looks like a lifetime bug.
`g_ffxAssetLoader` is `FFX_RomDev_EnqueueRead` RVA `0x0036B970`, and it copies `ctx[3]`, `ctx[4]`,
`ctx[5]`, `ctx[6]` into the queue entry before returning. The callback is later invoked with the
*queue entry* pointer, which is why `FFX_Ch_OnRomReadComplete` RVA `0x0042A6C0` reading `arg+12` and
`arg+20` resolves to the chrId and the type 1 that RomRead wrote into its own frame.

### The queue and who drains it

```
g_ffxRomReadQueue       RVA 0x00D27C80   64 entries x 56 bytes
g_ffxRomReadQueuePaths  RVA 0x01EFB6C0   64 x 128-byte host0: path strings
```

Entry layout: +0 size, +4 destination buffer, +8 asset index, +12/+16/+20/+24 the four copied user
dwords, +0x20 the completion callback, +0x24 a sequence number, +0x31 queued, +0x32 taken. Full
queue asserts `"q_cnt != READ_Q_CNT"` from `libmscd.c` line 1457.

`FFX_RomDev_EnqueueRead` ends with `FFX_RomDev_PostReadTask` RVA `0x00242950`, which hands
`(FFX_RomDev_ExecuteQueuedRead, slot)` to `AsyncLoadManager__enqueue`. That appends a request under
the manager's mutex and signals its semaphore, so **the file read itself runs on an AsyncLoadManager
worker thread**. `FFX_RomDev_ExecuteQueuedRead` RVA `0x00643740` opens
`g_ffxRomReadQueuePaths[slot]` with `FFX_File_OpenHost0`, reads `entry+0` bytes into `entry+4`,
closes, and then calls `FFX_AsyncLoad_PostCompletion` RVA `0x00242980` to put the entry on a
completion list.

Hex-Rays types `FFX_RomDev_ExecuteQueuedRead` as `__usercall (int a1@<edi>, size_t Size)`. **The edi
argument is an artefact.** `FFX_RomDev_PostReadTask` hands the function to `sub_637860`, which calls
it as `(*(fn*)a1)(*(a1+4))`, one cdecl stack argument, the slot index. `edi` is uninitialised at
entry and is only read on paths the PC build does not take.

**The callbacks do not run on the worker thread.** `FFX_AsyncLoad_DispatchCompletions` RVA
`0x002427F0` drains the completion list under a mutex and calls
`FFX_RomDev_InvokeReadCallback 0x0036E640` on each entry, which is
`(*(fn*)(entry+0x20))(entry)`. For a CHRDATA read that is `FFX_Ch_OnRomReadComplete` ->
`FFX_ChCache_SetLoading(1, chrId, 0)`, the single write that makes `FFX_Ch_DataReadSync` return 0.
`FFX_AsyncLoad_DispatchCompletions` has two kinds of caller:

- **`FFX_MainStep` RVA `0x00420AE0` calls it as its fourth statement**, before anything else in the
  step. So in normal play a ROM read completes at the top of a main step on the game thread.
- **the pump**, below, calls it inline on whatever thread is pumping.

### The pump, and the Hex-Rays error that hides it

`FFX_Ch_RomPump` RVA `0x0042A5E0` is four instructions:

```
push ebp / mov ebp, esp / pop ebp / jmp g_ffxRomDevPump
```

A tail jump that forwards the caller's argument slot. **Hex-Rays types it `int (void)` and prints
every call site as `FFX_Ch_RomPump()`, and that is wrong.** The raw disassembly of the callers:

```
00825DA0  test    edi, edi
00825DA2  jnz     short loc_825DC1     ; so edi == 0 on the fall-through
00825DA4  push    esi                  ; chrId
00825DA5  call    FFX_Ch_RomRead
00825DAA  push    edi                  ; <-- argument 0 to RomPump
00825DAB  call    FFX_Ch_RomPump
...
00825DD1  push    0                    ; <-- argument 0 to RomPump
00825DD3  call    FFX_Ch_RomPump
00825DD8  add     esp, 4               ; and the caller cleans it
```

and `FFX_Ch_LoadMotionSetSync` at `0x008368B8` does `push 0 / call FFX_Ch_RomPump / add esp, 4`.
The target `g_ffxRomDevPump` RVA `0x01F10C48` is always `FFX_RomDev_Pump` RVA `0x0036C490`, whose
entry is `call FFX_AsyncLoad_DispatchCompletions` then `dec [ebp+arg_0] / jz`:

- **mode 1, non-blocking.** Scan the 64 slots for any pending read. None, return 0. Otherwise signal
  the reader semaphore and return 1. Used by the movie player and `sceRead`, which push 1.
- **mode 0 (or anything but 1), blocking.** `FFX_RomDev_PumpBlocking` RVA `0x0036E660` loops:
  dispatch completions, scan for a pending slot, signal the semaphore, run the read task inline
  through `dword_1128ADC`, wait on the completion event, dispatch again. It returns only when all 64
  slots are idle.

**So `FFX_Ch_RomPump(0)` makes any ROM read synchronous from the caller's point of view, including
firing the completion callback, with no frame boundary.** That is the mechanism the whole answer
rests on, and it is in the shipped retail code on the normal path.

### The asset-loader slot table

For reference, since the indirect calls obscure it. `sub_76CFF0 RVA 0x0036CFF0` installs the PS2 DVD
slots, and when its argument is 0, which is the shipped PC case, it calls
`FFX_Asset_InstallPcLoaderSlots RVA 0x00642BA0` to overwrite most of them. `g_ffxAssetLoader` and
the pump slot are set unconditionally and are the same function in both modes.

| RVA | slot | PC value | what it does |
|---|---|---|---|
| `0x01F10C40` | 1 | `FFX_RomDev_EnqueueRead 0x0036B970` | enqueue a read |
| `0x01F10C48` | 2 | `FFX_RomDev_Pump 0x0036C490` | poll (1) or block (0) |
| `0x01F10C50` | 4 | `libmscd__f76C510 0x0036C510` | select current kind, sets base index |
| `0x01F10C54` | 5 | `FFX_Asset_GetSizeForIndex 0x006428A0` | size, 0 when absent. Opens the file |
| `0x01F10C58` | - | `0` | 0 = PC loose files, 1 = PS2 DVD |
| `0x01F10C98` | 22 | `FFX_Asset_GetPathForIndex 0x00642830` | path, one shared 255-byte static |

`FFX_Ch_FindRomEntry RVA 0x0042A640` has a **side effect worth knowing**: on a hit it calls slot 4
with `word_C49660[category]`, selecting the asset kind. Categories map to kinds 28, 29, 30, 31, 32,
33, 34 for `c`, `m`, `n`, `s`, `w`, `f`, `k`, and category 7 maps to 0, which is bogus. The returned
value is `rank * byte_C4967F` where `byte_C4967F` is 5, so each model owns a run of 5 asset indices
and the `.chr` blob is sub-index 0. This is why `FFX_Ch_RomRead` calls size and path immediately
after, with nothing in between.

---

## 3. Is there a function that adds a cache entry, or loads a `.chr` off the filesystem?

**Yes to the first, and it is `FFX_Ch_RomRead` itself** (see above), which is the honest answer to
the question as asked. But the function a plugin should actually call is one level up.

### `FFX_Ch_LoadChrData` already does the whole thing

The retail branch of `FFX_Ch_LoadChrData RVA 0x00425A40`, taken for every id that is not
`(chrId & 0xF000) == 0xF000`:

```c
int blob = FFX_ChCache_GetData(1, chrId);                    // entry+12
if (blob == 0) {
    FFX_Ch_RomRead(chrId);
    FFX_Ch_RomPump(0);                                       // BLOCKS
    blob = FFX_ChCache_GetData(1, chrId);
    if (blob == 0) { printf("chrdata %x is not exist\n"); return nullptr; }
}
if (FFX_ChCache_GetState(1, chrId) == 1)
    FFX_Ch_RomPump(0);                                       // BLOCKS
FFX_ChCache_Lock(1, chrId);
CHRDATA* d = FFX_Ch_BlkAllocate(chrId, blob);
...
return d;
```

Three things follow:

- **It never calls `FFX_Ch_DataReadSync`**, so the "does not read" assert cannot fire on this path.
- **It is synchronous.** `FFX_Ch_RomPump(0)` blocks, so the CHRDATA is usable when the call returns.
- **Failure is a clean `nullptr`** plus one `printf`.

### `FFX_Ch_ReadFileDev` is not the answer

`FFX_Ch_ReadFileDev RVA 0x0043AE30` is `Sg_PcRead(path, 0)` then a fixup, and it is a genuine
loose-file reader. But in `FFX_Ch_LoadChrData` it is used **only** on the
`(chrId & 0xF000) == 0xF000` viewer/prototype branch, building
`/ffx/proj2/chr/prot/<dir>/<name>/mdl/<name>.chr`, and `CHEAT_MODELS.md` section 1 already
established that the prototype string table is empty in retail. The other use is the NPC `.anm`
file. It bypasses the cache entirely, which also means it bypasses `FFX_Ch_BlkAllocate`'s cache-owned
blob accounting, so a `.chr` read this way would be owned by the caller. **There is no reason to use
it: the cached path is shorter, is the engine's own, and handles the blob lifetime.**

### What the shipped debug spawner does about residency

**Nothing.** This is the strongest single piece of evidence, because `FFX_Ch_DebugSpawnByName RVA
0x004295E0` is precisely the shipped "spawn an arbitrary model by name" path, and its residency
handling is:

```c
int chrId = (FFX_Ch_LetterToCategory(name[0]) << 12) | atoi(name + 1);
if (FFX_Ch_GetPlayerChr()) FFX_Player__getPos(x);
CHR* c = FFX_Ch_Allocate(chrId);        // <-- that is it. no RomRead, no DataReadSync, no poll
if (c) { FFX_Ch_SetPos(...); FFX_Ch_SetPlayerChr(c); /* then the per-category motion setup */ }
```

No `FFX_Ch_RomRead`, no `FFX_Ch_DataReadSync`, no pre-pass, no retry loop, no frame spread. The dev
team's answer to "how do I spawn any model from a debug menu" was that there is nothing to solve,
because `FFX_Ch_Allocate` -> `FFX_Ch_LoadChrData` -> `FFX_Ch_RomRead` + `FFX_Ch_RomPump(0)` is
already a complete synchronous load. The only per-category work in the whole function is the motion
setup afterwards, which `CHEAT_MODELS.md` section 3 already documents correctly.

---

## 4. What the "error box" actually does: nothing visible, and it is not fatal

`FFX_Ch_DataReadSync`'s state-2 branch is

```
0082A058  push [ebp+arg_0]
0082A05B  call FFX_Ch_IdToModelName
0082A060  push eax                         ; the full char*, one vararg for one %s
0082A061  push offset "%s does not read.(can't sync)"
0082A066  push offset "CH Data Read Sync"
0082A06B  push 0                           ; a1 == 0, the non-fatal flavour
0082A06D  call yiAssert
```

Hex-Rays prints `v2 = (unsigned __int8)FFX_Ch_IdToModelName(a1)` and that truncation is decompiler
noise, the raw pushes the pointer. The vararg count matches the format string, here and in the other
two cache asserts (`"Can't find cache yet=%d id=%s:%d [%s]"` pushes 4,
`"CacheData isn't red yet..."` pushes 3), so there is no varargs crash lurking in any of them.

`yiAssert RVA 0x0050AE50` in retail does this and only this:

| what the PS2 original did | what the PC build does |
|---|---|
| `sub_62F890(0)` x 120 twice, a vsync wait | `xor eax, eax / retn`. Free |
| `sub_820530` returns the gfx context | returns `&unk_12FB900`, never null, so the `a1 == 0` early return at the top is dead code |
| build a GS display list at `0x19350F0` and submit it | submitted through `nullsub_40`, a bare `retn` |
| `sub_90AAA0` draws the title and message as glyph quads | same, every submit goes through `nullsub_40` / `sub_62F6E0` / `sub_62F730` / `sub_62F880`, all of which are `retn` or `xor eax,eax / retn` |
| `SG_Printf_STUB` x 9 | `retn` |
| abort | `nullsub_35`, a bare `retn`, on both the `a1 == 0` and `a1 != 0` tails |
| `vprintf(Format, va)` | **the one thing that still happens**, to stdout |

**So it is not a box, not modal, does not halt the simulation, costs no frames, and returns.** It is
a log line. A UI can call `FFX_Ch_DataReadSync` on a speculative id and recover.

One genuine hazard in `yiAssert`, unrelated to our path but worth recording:

```c
if (strlen(title) > 0x2E) *((BYTE*)title + 46) = v14;    // v14 is UNINITIALISED
```

For a title longer than 46 characters that writes a garbage byte into the caller's title string,
which is usually a `.rdata` literal, so an access violation. `"CH Data Read Sync"` and
`"Memory Cache"` are 17 and 12 characters, so neither fires.

The one real cost of the DataReadSync route is a log flood, not a crash. If `FFX_Ch_RomRead`
returned -1 because the id is not in the ROM index, no entry is created, so a per-frame poll of
`FFX_Ch_DataReadSync` prints every frame forever. That is what `FFX_AtelSys_Ch_100_poll` would do
with a bad id. Use the pre-flight test in section 5 instead.

---

## 5. The call sequence, the costs, and the failure modes

### The sequence

All of this must be on the **game thread**. `FFX_Ch_RomPump(0)` mutates `g_ffxRomReadQueue` state
bytes with no lock and races `FFX_MainStep`'s own `FFX_AsyncLoad_DispatchCompletions`, and
`FFX_Ch_BlkAllocate` walks `g_ffxChrDataTable` unguarded. The loader's existing `animate` vtable
hook or a `FFX_BootTask_Push` task are both fine.

```c
// base = GetModuleHandle(NULL), the exe is ASLR'd.
typedef int   (__cdecl *fnFindRomEntry)(int chrId);
typedef int   (__cdecl *fnGetSize)(int romIndex);
typedef int   (__cdecl *fnRomRead)(int chrId);
typedef int   (__cdecl *fnRomPump)(int mode);          // NOT (void). see section 2
typedef void* (__cdecl *fnLoadChrData)(int chrId);
typedef void  (__cdecl *fnSetNoFallbackOnce)(void);
typedef void* (__cdecl *fnChAllocate)(int chrId);

#define RVA(t, r) ((t)((char*)base + (r)))

// ---- 0. pre-flight. this is EXACTLY the condition RomRead itself tests, no guessing.
//      FindRomEntry also selects the asset kind, so GetSize must follow it immediately
//      with nothing in between. Do this pair back to back, and cache the result, because
//      FFX_Asset_GetSizeForIndex opens the file to measure it.
int rom = RVA(fnFindRomEntry, 0x0042A640)(chrId);
if (rom < 0)
    return NOT_IN_ROM_INDEX;                           // e.g. c307
int size = (*(fnGetSize*)((char*)base + 0x01F10C54))(rom);
if (size == 0)
    return FILE_DOES_NOT_SHIP;                         // e.g. c046

// ---- 1. headroom checks. both of these are null-deref crashes if you skip them.
//      CHRDATA table: 40 records of 300 bytes, free == record[0] == -1.
{
    const int*  rec   = (const int*)((char*)base + 0x01FC8D00);
    int         free_ = 0;
    for (int i = 0; i < 40; ++i)
        if (rec[i * 75] == -1) ++free_;
    if (free_ == 0)
        return NO_CHRDATA_SLOT;                        // FFX_Ch_BlkAllocate would memset(NULL)
}
//      CHR pool, the check CHEAT_MODELS.md section 3 already specifies.
if (FFX_Ch_CountLive() >= *(int*)((char*)base + 0x01FC44E0))
    return NO_CHR_SLOT;

// ---- 2. make it resident. ONE CALL, and it blocks until done.
//      Either of these two is sufficient and they are equivalent:
//        (a) let Allocate do it, which is what FFX_Ch_DebugSpawnByName does
//        (b) call LoadChrData yourself first, if you want the CHRDATA pointer
//            before committing to an allocation
void* chrData = RVA(fnLoadChrData, 0x00425A40)(chrId);
if (chrData == NULL)
    return LOAD_FAILED;                                // prints "chrdata %x is not exist"

// ---- 3. allocate. ask for a hard failure instead of a silent Tidus.
RVA(fnSetNoFallbackOnce, 0x004285D0)();                // one-shot, Allocate clears it
void* neo = RVA(fnChAllocate, 0x00424F90)(chrId);
if (neo == NULL)
    return ALLOC_FAILED;

// ---- 4. motions and the rest of the swap: unchanged from CHEAT_MODELS.md section 3.
//      FFX_Ch_LoadMotionSetSync 0x00436870 is also self-sufficient, it calls
//      FFX_Ch_RomPump(0) internally. Mode 1 for mon and sum, mode 0 otherwise,
//      and the /ffx/npcanm/<name>.anm branch for npc.
```

Step 2 is optional if you are going to allocate anyway, because `FFX_Ch_Allocate` calls
`FFX_Ch_LoadChrData` on its own. Keep it when you want to separate "did the model load" from "did
the CHR allocate" for the UI.

What is **not** needed, contradicting the sequence currently in `CHEAT_MODELS.md` section 3:

- `FFX_Ch_RomRead(newId)` followed by a `FFX_Ch_DataReadSync(newId) == 1` retry across frames. If
  you want the explicit form, write `FFX_Ch_RomRead(id); FFX_Ch_RomPump(0);` and it is done in one
  frame. The retry-next-frame shape is for the ATEL script's benefit, not a plugin's.
- `FFX_Ch_MotionSetReadStart` then polling `FFX_Ch_MotionSetReadSync`. `FFX_Ch_LoadMotionSetSync`
  blocks on its own pump, same as `FFX_Ch_DebugSpawnByName` relies on. For the record,
  `FFX_Ch_MotionSetReadSync RVA 0x00436A50` maps state 2 to 0 and never asserts, so even the polled
  form is harmless there.

### Frames

**Zero extra frames for the data.** `FFX_Ch_RomPump(0)` blocks, so the CHRDATA, the skeleton, the
part table, the scale and the motion sets all exist by the time your call returns, within the step
you called from.

**One to a few frames for the visible mesh, and this is unavoidable.** `FFX_Ch_LoadModel RVA
0x0042A1C0` calls `FFX_LoadCharacterData RVA 0x0023D140`, which allocates a `ClassCharacter` and then
`FFX_EnqueueCharacterLoad RVA 0x002642F0` posts a Phyre load to the AsyncLoadManager. The handle is
returned immediately and stored into cache entry +16, so `FFX_ChCache_GetModel` works at once and
`FFX_Ch_Allocate` succeeds, but the render instance is only created once the Phyre data has arrived.
`FFX_Chr_ProcessPendingAttachments RVA 0x00239DB0`, called every `FFX_MainStep`, drains the pending
list and calls `ClassCharacter__createInstance`. **The engine's own CHRREG path has exactly the same
property**, so this is the normal appearance latency and not a plugin artefact. `FFX_Ch_OnModelLoaded
RVA 0x0042A140` fills the `.cdf`/`.cmf` slots at +24/+28 on a later frame too, and nothing in
`FFX_Ch_Allocate` reads them.

### Costs

- **Wall clock.** One synchronous open-read-close of the `.chr` on the game thread, inside
  `FFX_Ch_RomPump(0)`, plus the same for each motion set you load. A visible hitch for a large
  monster. Doing it from a UI keypress is fine. Doing it per frame in a scrollable preview is not.
- **Memory, per newly resident model.** One `FFX_MemAlloc` of `((size + 0x8F) & ~0x7F) - 0x10` for
  the blob, held by the cache entry. One of the 40 CHRDATA records, 300 bytes. One of the 100 cache
  slots. For a monster, `FFX_Ch_BlkAllocate` additionally `FFX_MemAlloc`s and `memcpy`s the effect
  data block.
- **Not a leak, but not promptly freed either.** The next map transition runs
  `FFX_Ch_EventjumpDispose` -> `FFX_Ch_DataDisposeAll` -> `FFX_Ch_DataFree`, which disposes the four
  motion sets and calls `FFX_ChCache_Unlock(1, id)`. Unlock only marks the entry evictable. The blob
  is actually freed later, either by `FFX_ChCache_EvictOne` when a subsequent `CreateEntry` needs the
  slot, or by `FFX_ChCache_FreeUnlockedEntries`. So blob memory can sit around for a while after you
  are done with it, bounded by the 100 slots.

### Failure modes, each one verified

| what | symptom | guard |
|---|---|---|
| chrId not in `g_ffxChrRomIndexTables` | `FFX_Ch_RomRead` returns -1, no entry, `FFX_Ch_LoadChrData` returns null, `FFX_Ch_Allocate` silently substitutes `c001` with `m_isFallback = 1` | pre-flight `FFX_Ch_FindRomEntry >= 0`, and `FFX_Ch_SetNoFallbackOnce` |
| file does not ship (e.g. `c046`) | `g_ffxRomDevGetSize` returns 0, same as above | pre-flight `size != 0` |
| `g_ffxChrDataTable` full, all 40 taken | **hard crash.** `FFX_Ch_BlkAllocate` leaves its record pointer null and the next statement is `memset(NULL, 0, 300)` | count records with `[0] == -1` first |
| CHR pool full | **hard crash**, `FFX_Ch_Allocate` memsets `0x880` bytes through null. Already in `CHEAT_MODELS.md` | compare live count to `0x01FC44E0` |
| all 100 cache slots taken and nothing evictable | **silent no-op.** `FFX_ChCache_CreateEntry`'s loop is `if (EvictOne() == 0) continue; break;`, so a failed eviction breaks out having created nothing. `FFX_Ch_RomRead` then calls `SetLoading` on a missing entry, which prints `"Can't find cache yet"`, the read still lands in the buffer, and the completion prints it again. No return code says so | only reachable under real pressure. If you see that string, stop loading models |
| heap exhausted | **hard crash.** `FFX_MemAllocLocked RVA 0x002FB910` ends with an unconditional `memset(p, 0xCD, Size)` | nothing to do but keep the working set small |
| 64-slot read queue full | assert `"q_cnt != READ_Q_CNT"`, which is a printf | not reachable from a UI that loads one model at a time |
| polling `FFX_Ch_DataReadSync` on an id `RomRead` refused | one `vprintf` per frame forever | do not use the polled route |

### The honest caveats that are not about residency

These are not residency problems, but they decide what the UI should promise, and two of them are
already settled in `CHEAT_MODELS.md` section 3 and still apply unchanged:

- **The swap does not survive a map change or a cutscene.** The ATEL actor's model comes from
  `actor+168` and `FFX_AtelOp_SetActorModel` puts it back. Say "lasts until the next map".
- **`g_ffxTidusChr` must be repointed before the old body is disposed.** Unchanged.
- **Monsters and aeons need the mode 1 motion setup.** Unchanged, and it is visible in
  `FFX_Ch_DebugSpawnByName`.

### For the picker: how to mark availability at runtime

The answer to "which subset is reliably loadable at any moment" is **all 892 of them**, so the
picker does not need an availability column driven by residency. What it does usefully need is a
"this entry is dead" column, and there is an exact runtime test for it rather than the
load-then-detect-fallback trick:

```c
// exact, cheap-ish, no side effects beyond clobbering the loader's current-kind globals.
// Run it at a safe point on the game thread and cache the answer per id: GetSizeForIndex
// opens the file.
static int ChrIsLoadable(void* base, int chrId)
{
    int rom = RVA(fnFindRomEntry, 0x0042A640)(chrId);
    if (rom < 0) return 0;
    return (*(fnGetSize*)((char*)base + 0x01F10C54))(rom) != 0;
}
```

892 calls is 892 file opens, so do it lazily as rows scroll into view, or once at startup behind a
progress line, and cache it. The two known disagreements `CHEAT_MODELS.md` found drop out of this
automatically: `c046` fails on `size == 0`, `c307` fails on `rom < 0`.

And if you want a genuine "resident right now" badge, for instance to show which models are free to
switch to with no hitch, walk the cache directly:

```c
void** slots = (void**)((char*)base + 0x00F02F98);   // 100 pointers, fixed count
for (int i = 0; i < 100; ++i)
{
    const unsigned char* e = (const unsigned char*)slots[i];
    if (!e) continue;
    if (*(void* const*)(e + 12) == NULL) continue;   // no blob, slot is free
    if (e[4] != 1) continue;                         // not CHRDATA
    int  chrId     = *(const int*)e;
    int  isLoading = e[6] != 0;
    int  isLocked  = e[5] != 0;
    // resident and ready == !isLoading
}
```

### One correction and one non-correction for `CHEAT_MODELS.md`

- **Correction.** Section 3's call sequence should drop the `FFX_Ch_RomRead` plus
  `FFX_Ch_DataReadSync == 1` retry and the `FFX_Ch_MotionSetReadStart`/`ReadSync` pair, and its note
  that state 2 "pops an error box" is wrong in retail. The replacement is in this document.
- **Non-correction.** Section 2's claim that `c307` cannot be loaded at runtime **stands**, and I
  checked a path that looked like it might overturn it. `FFX_RomDev_EnqueueRead` contains a
  hardcoded hack at VA `0x0076BBF7` that, when `g_ffxSaveData[0] == dword_CCC7C8`, rewrites the
  resolved path `host0:/ffx/master/jppc/chr/pc/c101/mdl/c101.chr` in place to the `c307` equivalent.
  But `dword_CCC7C8` RVA `0x008CC7C8` initialises to -1 and its only writer, `sub_AEF760`, is
  reachable solely through a function-pointer table entry at `0x0070D024`. In retail it stays -1,
  the byte compare can never match, and the rewrite is dead code. Recorded so nobody chases it
  twice.

---

## Addresses to promote into the kit

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| `0x0042A5E0` | func | `ChRomPump` | `int __cdecl (int mode)` | **TAKES AN ARGUMENT**, Hex-Rays says otherwise. mode 0 blocks until every pending ROM read is done and its callback has run, mode 1 polls |
| `0x00425A40` | func | `ChLoadChrData` | `CHRDATA * __cdecl (int chrId)` | THE residency entry point. Self-sufficient, synchronous, never asserts, returns null on failure |
| `0x00429EF0` | func | `ChRomRead` | `int __cdecl (int chrId)` | creates the cache entry and starts the read. 0 = accepted or already present, -1 = refused. Idempotent |
| `0x0042A040` | func | `ChDataReadSync` | `int __cdecl (int chrId)` | 0 ready, 1 loading, 2 no entry. The 2 is a printf, not a box. Prefer the two above |
| `0x0042A640` | func | `ChFindRomEntry` | `int __cdecl (int chrId)` | -1 = not in the index. SIDE EFFECT: selects the asset kind. Returns `rank * 5` |
| `0x004285D0` | func | `ChSetNoFallbackOnce` | `void __cdecl (void)` | one-shot. Makes the next `FFX_Ch_Allocate` return null instead of substituting `c001` |
| `0x00425760` | func | `ChBlkAllocate` | `CHRDATA * __cdecl (int chrId, void *blob)` | **null-deref crash when all 40 records are taken.** Count free records first |
| `0x004258D0` | func | `ChDataFree` | `int __cdecl (CHRDATA *)` | disposes the 4 motion sets and calls `ChCacheUnlock(1, id)` |
| `0x004259C0` | func | `ChDataDisposeAll` | `int __cdecl (void)` | frees all 40 records. One caller, `ChEventjumpDispose` |
| `0x0043B420` | func | `ChEventjumpDispose` | `void __cdecl (void)` | the per-map-transition teardown that releases plugin-loaded models |
| `0x00425EA0` | func | `ChFindChrData` | `CHRDATA * __cdecl (int chrId)` | linear scan of the 40-record table, keyed on chrId |
| `0x00436870` | func | `ChLoadMotionSetSync` | `void __cdecl (int chrId, int mode)` | also self-sufficient, blocks on `ChRomPump(0)` internally |
| `0x00436A50` | func | `ChMotionSetReadSync` | `int __cdecl (int chrId, int mode)` | maps state 2 to 0, never asserts. Safe to poll |
| `0x0042A4C0` | func | `ChRomReadMotionSet` | `int __cdecl (int chrId, int mode)` | cache type 2, key `chrId \| (mode << 16)`. 0 = ok, non-zero = refused |
| `0x0043FFE0` | func | `ChCacheGetState` | `int __cdecl (int type, int key)` | the definition of resident. 100-slot linear scan |
| `0x00440790` | func | `ChCacheGetData` | `void * __cdecl (int type, int key)` | returns entry+12, the raw blob. No assert |
| `0x004406F0` | func | `ChCacheGetDataChecked` | `void * __cdecl (int type, int key)` | same but printfs when still loading |
| `0x00440890` | func | `ChCacheGetModel` | `void * __cdecl (int type, int key)` | entry+16, the primary `ClassCharacter *` |
| `0x004408D0` | func | `ChCacheGetModel2` | `void * __cdecl (int type, int key)` | entry+20. Returns 0 right after a fresh `RomRead`, and `ChAllocate` guards for it |
| `0x00440560` | func | `ChCacheCreateEntry` | `void __cdecl (char type, int key, void *blob)` | **silently creates nothing when all 100 slots are taken and none is evictable** |
| `0x004405F0` | func | `ChCacheSetLoading` | `char __cdecl (int type, int key, char loading)` | writes entry+6. Printfs `"Can't find cache yet"` when the entry is gone |
| `0x00440DD0` | func | `ChCacheLock` | `void __cdecl (int type, int key)` | entry+5 = 1, blocks eviction. `ChLoadChrData` always locks |
| `0x00440D70` | func | `ChCacheUnlock` | `void __cdecl (int type, int key)` | entry+5 = 0. Marks evictable, does NOT free |
| `0x004409A0` | func | `ChCacheEvictOne` | `int __cdecl (void)` | returns 1 when nothing could be evicted. Only caller is `CreateEntry` |
| `0x00440030` | func | `ChCacheFreeUnlockedEntries` | `void __cdecl (void)` | frees every unlocked entry in one pass |
| `0x00440680` | func | `ChCacheGetModelSlots` | `int * __cdecl (int, int, void**, void**, void**, void**)` | zeroes and hands back pointers to entry +16/+20/+24/+28 |
| `0x0042A6C0` | func | `ChOnRomReadComplete` | `char __cdecl (void *queueEntry)` | `SetLoading(entry[20], entry[12], 0)`. Runs on the pumping thread |
| `0x0042A1C0` | func | `ChLoadModel` | `void * __cdecl (const char *path, void **cdf, void **cmf, int chrId)` | the Phyre mesh. Returns a handle at once, data arrives async |
| `0x0042A140` | func | `ChOnModelLoaded` | `int __cdecl (char *pendingRec)` | fills the `.cdf`/`.cmf` slots on a later frame |
| `0x0023D140` | func | `LoadCharacterData` | `void * __cdecl (int, const char *path, int assetId)` | cache lookup then `EnqueueCharacterLoad`. Null on a missing asset |
| `0x002642F0` | func | `EnqueueCharacterLoad` | `int __stdcall (const char *path, void *classChar)` | posts the Phyre load to the AsyncLoadManager. 0 = queued |
| `0x00239DB0` | func | `ChrProcessPendingAttachments` | `char __cdecl (void)` | called every `FFX_MainStep`, creates the render instance once Phyre has the data |
| `0x0036B970` | func | `RomDevEnqueueRead` | `int __cdecl (int assetIdx, void *dst, void *cb, int *ctx)` | `g_ffxAssetLoader`'s target. Copies `ctx[3..6]` into the queue entry |
| `0x0036C490` | func | `RomDevPump` | `int __cdecl (int mode)` | `g_ffxRomDevPump`'s target. The `dec [ebp+arg_0] / jz` is the poll/block branch |
| `0x0036E660` | func | `RomDevPumpBlocking` | `void * __cdecl (void)` | spins until all 64 queue slots are idle, dispatching completions inline |
| `0x00643740` | func | `RomDevExecuteQueuedRead` | `void __cdecl (int slot)` | the actual open-read-close, on the worker thread. The Hex-Rays `edi` arg is an artefact |
| `0x00242950` | func | `RomDevPostReadTask` | `int __cdecl (void *fn, int arg)` | hands the read to the AsyncLoadManager worker |
| `0x002427F0` | func | `AsyncLoadDispatchCompletions` | `int __cdecl (void)` | **called as the 4th statement of `FFX_MainStep`.** Runs the completion callbacks |
| `0x00242980` | func | `AsyncLoadPostCompletion` | `int __cdecl (void *queueEntry)` | worker side, queues the entry for the game thread |
| `0x0036E640` | func | `RomDevInvokeReadCallback` | `int __cdecl (void *queueEntry)` | `(*(fn*)(entry+0x20))(entry)` then frees the slot |
| `0x006428A0` | func | `AssetGetSizeForIndex` | `unsigned __cdecl (int assetIdx)` | 0 when absent. Opens the file to measure it, so cache the result |
| `0x00642830` | func | `AssetGetPathForIndex` | `char * __cdecl (int assetIdx)` | returns ONE shared 255-byte static buffer |
| `0x00642BA0` | func | `AssetInstallPcLoaderSlots` | `void __cdecl (void)` | overwrites the DVD slots with the PC ones, the shipped case |
| `0x0036CFF0` | func | `AssetInstallLoaderSlots` | `int __cdecl (int dvdMode)` | installs the slot table. 0 picks the PC path |
| `0x0050AE50` | func | `yiAssert` | `void __cdecl (int fatal, const char *title, const char *fmt, ...)` | a `vprintf` in retail. No box, no abort, no frame cost. Title longer than 46 chars corrupts it |
| `0x00461850` | func | `FieldChrRegTask` | `int __cdecl (DWORD *)` | the per-map populate. Already in `CHEAT_MODELS.md` |
| `0x00679800` | func | `AtelSysChResi` | `int __cdecl (int, int, int *)` | ATEL lib 5 func 78, the script's `RomRead` |
| `0x00679290` | func | `AtelSysChReadSyncPoll` | `int __cdecl (int, int *)` | ATEL lib 5 func 100, the matching poll |
| `0x004295E0` | func | `ChDebugSpawnByName` | `void __cdecl (const char *name)` | the shipped arbitrary-model spawner. Does NOTHING about residency |
| `0x002FB910` | func | `MemAllocLocked` | `void * __cdecl (size_t)` | unconditional `memset(p, 0xCD, size)` at the end, so OOM is a crash |
| `0x00F02F98` | data | `ChrCacheSlots` | `void *[100]` | the cache. Count is a compile-time immediate, not a variable |
| `0x00F03128` | data | `ChrCacheSlotsEnd` | end address | the exclusive bound the scans compare against |
| `0x01FC8D00` | data | `ChrDataTable` | `40 x 300` | **the real cap on distinct live models.** Free record is `[0] == -1` |
| `0x01FCBBE0` | data | `ChrDataTableEnd` | end address | `0x1FCBBE0 - 0x1FC8D00 = 12000 = 40 * 300` |
| `0x00D27C80` | data | `RomReadQueue` | `64 x 56` | +0 size, +4 dst, +8 assetIdx, +12..+24 user dwords, +0x20 cb, +0x31 queued |
| `0x01EFB6C0` | data | `RomReadQueuePaths` | `64 x 128` | the resolved `host0:` path per queue slot |
| `0x01F10C40` | data | `AssetLoaderEnqueue` | `int (__cdecl *)(int, void*, void*, int*)` | slot 1. Always `0x0036B970` |
| `0x01F10C48` | data | `AssetLoaderPump` | `int (__cdecl *)(int mode)` | slot 2. Always `0x0036C490`. `ChRomPump` tail-jumps here |
| `0x01F10C50` | data | `AssetLoaderSelectKind` | `int (__cdecl *)(int kind)` | slot 4. Sets the current kind and base index |
| `0x01F10C54` | data | `AssetLoaderGetSize` | `unsigned (__cdecl *)(int)` | slot 5. PC = `0x006428A0`. The exact pre-flight test |
| `0x01F10C58` | data | `AssetLoaderDvdMode` | `int` | 0 = PC loose files (shipped), 1 = PS2 DVD |
| `0x01F10C98` | data | `AssetLoaderGetPath` | `char *(__cdecl *)(int)` | slot 22. PC = `0x00642830` |
| `0x00EFBD98` | data | `ChrNoFallbackOnce` | `int` | one-shot. Non-zero makes `ChAllocate` return null on a missing model |
| `0x00849660` | data | `ChrCategoryToAssetKind` | `WORD[8]` | 28, 29, 30, 31, 32, 33, 34, 0 for `c m n s w f k` and the bogus 7 |
| `0x0084967F` | data | `ChrAssetSubFileCount` | `BYTE` | 5. `ChFindRomEntry` returns `rank * 5`, the blob being sub-index 0 |
| `0x008CC7C8` | data | `ChrC101ToC307Gate` | `int` | the dead `c101 -> c307` path-rewrite gate. Stays -1 in retail |
