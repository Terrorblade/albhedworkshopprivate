# Cheat plugin: character models, party composition and minigames

What a cheat plugin needs to offer three things: swap Tidus's model for any model in the
game, set the party to any supported members, and start any minigame. Plus a late
addition that turned out to matter most, edit the variables of whatever minigame is
running right now.

**The rule that shaped this document: every option in the UI is built from GAME DATA.**
Nothing is typed by hand. Typing filters a list, it never produces an entry. So for each
of the three pickers the question is not "what is the function" but "where is THE LIST",
and that is what each section leads with.

Addresses here are **RVAs**, which is the IDA VA minus `0x400000`. Where a VA is quoted
it is marked as one. `reversing/PHASE1_CLONE.md` quotes VAs throughout, so converting
between the two documents needs care.

---

## 1. The chrId encoding, settled

```c
chrId = ((category & 0xF) << 12) | (number & 0xFFF)
```

**Category is bits 12 to 15, a single nibble. The model number is bits 0 to 11.** That
is not inferred, it is what the three functions that handle the id literally do:

| function | VA | RVA | what it proves |
|---|---|---|---|
| `FFX_Ch_IdToModelName` | `0x838100` | `0x438100` | `sprintf("%c%03d", letter(id >> 12 & 0xF), id & 0xFFF)` |
| `FFX_Ch_DebugSpawnByName` | `0x8295E0` | `0x4295E0` | `id = (letterToCategory(name[0]) << 12) | atoi(name + 1)` |
| `FFX_Ch_FindRomEntry` | `0x82A640` | `0x42A640` | `cat = (id >> 12) & 0xF; num = id & 0xFFF;` and rejects `cat > 7` |

The seven categories, from `FFX_Ch_LetterToCategory 0x829D80` and its inverse
`FFX_Ch_CategoryToLetter 0x829E10`, with the asset directory from
`FFX_Ch_CategoryToDirName 0x829C90`:

| category | letter | dir | what it is | id range |
|---|---|---|---|---|
| 0 | `c` | `pc` | playable characters | `0x0001` to `0x0FFF` |
| 1 | `m` | `mon` | monsters | `0x1000` to `0x1FFF` |
| 2 | `n` | `npc` | NPCs | `0x2000` to `0x2FFF` |
| 3 | `s` | `sum` | summons, the aeons | `0x3000` to `0x3FFF` |
| 4 | `w` | `wep` | weapons | `0x4000` to `0x4FFF` |
| 5 | `f` | `obj` | objects | `0x5000` to `0x5FFF` |
| 6 | `k` | `skl` | skeletons | `0x6000` to `0x6FFF` |

Three things about the edges that a picker has to respect:

- **Category 7 to 14 do not exist.** `FFX_Ch_CategoryToLetter` returns `'-'` and
  `FFX_Ch_CategoryToDirName` returns the empty string, so the id formats as `"-042"` and
  the path does not resolve. `FFX_Ch_FindRomEntry` allows `cat <= 7` but
  `g_ffxChrRomIndexTables` only has 7 entries loaded, so category 7 reads a stale
  pointer. **Clamp the picker to 0..6.**
- **Category 15 (`0xF`) is a real category and it is the asset viewer.** It does not go
  through the ROM index at all, see the separate branch in `FFX_Ch_LoadChrData`. Ids are
  handed out at runtime by `sub_826DC0 0x826DC0`, which returns `index + 0xF000`, and
  resolve to a prototype path built from a runtime string table at VA `0x12FBCA8` (RVA
  `0xEFBCA8`, stride 12). Those are prototype assets and the table is empty in retail.
  Not useful for the cheat UI, but worth knowing it is why `(chrId & 0xF000) == 0xF000`
  is special-cased.
- **The number is 12 bits but the name format is `%03d`.** A number above 999 formats as
  a 4-digit name that matches no shipped path. No shipped table has one, so in practice
  the number is 0 to 999.
- `chrId == 0` is special-cased by `FFX_Ch_IdToModelName` to the literal string `"0000"`.

So the `0x3011` seen in `magic_0162.dll` in `PHASE1_CLONE.md` is category 3 `sum`,
number 17, model name `s017`. That reading is confirmed.

---

## 2. THE MODEL LIST, and it can be built at runtime in process

This is the headline answer, and it needs no baked data at all.

**`g_ffxChrRomIndexTables` RVA `0x00EFFAB8` (VA `0x12FFAB8`) is a `void *[7]` indexed by
category.** Each entry points at a loaded per-category index table whose layout is

```
s16  count
s16  modelNumber[count]
```

It is populated by `FFX_Ch_LoadRomIndexTables 0x82A5F0` (RVA `0x42A5F0`), which is called
from `FFX_Ch_Init 0x826EA0` **unconditionally, with no debug-mode gate**, in a
`for (i = 0; i < 7; ++i)` loop. The consumer is `FFX_Ch_FindRomEntry 0x82A640`, which is
the single gate every model load passes through: it scans the table for
`chrId & 0xFFF` and returns -1 if it is not there, and on -1 `FFX_Ch_RomRead` gives up.

**So this table is not merely a list of models, it is the definitive statement of what
the engine is able to load.** That is exactly what a picker wants.

### The enumeration loop the plugin should use

```c
// base = GetModuleHandle(NULL), the exe is ASLR'd
void** romTables = (void**)(base + 0x00EFFAB8);
static const char kLetter[7] = { 'c', 'm', 'n', 's', 'w', 'f', 'k' };

for (int cat = 0; cat < 7; ++cat)
{
    const short* t = (const short*)romTables[cat];
    if (!t)
        continue;                       // not loaded yet, see the timing note below
    int count = t[0];
    if (count <= 0 || count > 4096)
        continue;                       // sanity, the table is read from a file
    for (int i = 0; i < count; ++i)
    {
        int number = (unsigned short)t[1 + i];
        int chrId  = (cat << 12) | number;
        char label[8];
        sprintf(label, "%c%03d", kLetter[cat], number);   // "c001", "m042"
        // offer (chrId, label, dirNameFor(cat)) in the list
    }
}
```

**Format the name yourself rather than calling `FFX_Ch_IdToModelName`.** That function
`sprintf`s into one static buffer, VA `0x1300A00` (RVA `0xF00A00`), and returns a pointer
to it. Every call overwrites the previous answer, so building a list of 892 labels by
calling it 892 times leaves 892 pointers to the same string. The format is four
characters, it is not worth the trap.

### Timing

`FFX_Ch_Init` runs once at startup, before any map. The `animate` vtable hook the loader
already has (RVA `0x0070D9A8`) runs long after, so by the time any plugin UI exists the
tables are up. Guard on the pointer being non-null anyway, because that is free.

### What actually ships, counted

The seven tables are the files `ffx_ps2/ffx/master/jppc/chr/<dir>/<dir>.tbl` in
`data/FFX_Data.vbf`. Read with `tools/vbf.py`, which already handles the archive. Their
contents, confirmed byte for byte against the `count`-then-`u16[]` layout the
disassembly implies:

| category | file | bytes | count | numbers |
|---|---|---|---|---|
| 0 `pc` | `chr/pc/pc.tbl` | 72 | **35** | 1-8, 41, 43-46, 51, 101-108, 121, 122, 901-908, 921, 922, 999 |
| 1 `mon` | `chr/mon/mon.tbl` | 698 | **348** | 0-346 contiguous, plus 999 |
| 2 `npc` | `chr/npc/npc.tbl` | 480 | **239** | sparse, 1 to 356, plus 999 |
| 3 `sum` | `chr/sum/sum.tbl` | 64 | **31** | 1-31 contiguous |
| 4 `wep` | `chr/wep/wep.tbl` | 162 | **80** | sparse, 1 to 103 |
| 5 `obj` | `chr/obj/obj.tbl` | 236 | **117** | sparse, 1 to 176 |
| 6 `skl` | `chr/skl/skl.tbl` | 86 | **42** | 1-7, 12-17, 66, 1xx, 2xx, 3xx, 4xx, 5xx, 998, 999 |

**892 models total.** For comparison, 859 `.chr` files actually exist in the archive
(`tools/pcmodels.py --all` walks them). The two counts disagree in both directions and
both disagreements matter:

- **`c046` is in `pc.tbl` but no `c046` asset ships at all.** Searching the whole archive
  for `c046` returns nothing. So the ROM table over-promises. This does **not** crash:
  `FFX_Ch_LoadChrData` returns null, and `FFX_Ch_Allocate` falls back to
  `FFX_Ch_LoadChrData(1)` (Tidus) and sets `CHRDATA.m_isFallback = 1`. **The plugin can
  detect a silent fallback by reading that byte after the allocation**, which is the
  honest way to show "this entry is dead" in the UI rather than pre-filtering.
- **`c307` ships a full `.chr`, `.mgrp` and Phyre model but is NOT in `pc.tbl`.** So it
  cannot be loaded at runtime at all, because `FFX_Ch_FindRomEntry` returns -1 for it.
  An unused asset. Do not put it in the list.

That asymmetry is the argument for building the list from `g_ffxChrRomIndexTables` rather
than from the archive directory. The archive tells you what shipped, the ROM table tells
you what the engine will load, and the engine is the one that has to agree.

### A second, debug-only list that is NOT the one to use

`FFX_Ch_Init` has a parallel path worth recording so nobody chases it twice. Gated on
`g_ffxDebugMode` RVA `0x00F3C910`, it loads `/ffx/proj2/chr/common/<dir>.tbl` into
`dword_12FBC64[cat]` (RVA `0xEFBC64`) with the record count in `dword_12FBC84[cat]`
(RVA `0xEFBC84`), at **stride 128 bytes** (`size >> 7`), where record `+0` is the model
number and record `+48` is a source-tag string. `FFX_Ch_LoadChrData` uses it to fill
`CHRDATA.m_sourceTag` and to print a proper asset path.

That would be a nicer list, because it carries a string per model. **It is useless in
retail: `/ffx/proj2/chr/common/` does not exist in the archive at all** (searching for
`chr/common` returns zero matches), so both arrays stay 0 even with debug mode on. The
shipped `<dir>.tbl` files are at `chr/<dir>/<dir>.tbl` and are the 2-byte-per-entry ROM
index described above, not this 128-byte-record format.

### Labels: what is honestly available

The engine's own label for a model is the four-character name, `"c001"` or `"m042"`, plus
the category directory name. That is what every log line, every path and the shipped
asset viewer use, and it is the only label that exists for all 892 entries.

Human names exist for a small subset and should be merged in where they do:

- **The 8 playable characters** get names from `DebugCharNames` RVA `0x0083432C` and the
  localised `CharNames` RVA `0x00D32DDC`. See section 4.
- **The aeons** are `sum` category models and the character-index table maps them. See
  section 4.
- **Monsters have names, but in a different id space.** `battle/kernel/monster1.bin`,
  `monster2.bin` and `monster3.bin` carry the bestiary records and `name_txt.bin` the
  name strings, but those are indexed by battle monster TYPE id, not by `.chr` model
  number. Those two spaces are related but the mapping was not established here, so **do
  not fabricate a monster name from a model number.** Show `m042 (mon)` and let typing
  filter on it. Flagged as unsettled rather than guessed.

So the picker shape that follows from the data is: group by category using the seven
directory names, show the four-character code as the primary label, append a friendly
name only for the 8 characters and the aeons, and let the filter box match either.

---

## 3. Swapping Tidus's model

### Recommendation: (a), change the chrId before allocation

Of the three options in the brief, **(a) is the right one and it is not close.** The
reason is structural and visible in the allocation path: **every per-model indirection in
the engine is keyed off the chrId, or off the CHRDATA that the chrId resolved to.**
Nothing about a CHR's appearance or animation is fixed at a level above the id. So
handing `FFX_Ch_Allocate` a different id produces a correct, fully animated character of
that model with no retargeting fixups at all.

That is worth spelling out, because the obvious worry is the one the brief raises:
motion sets are per model, so does a swapped model get driven by Tidus's animations and
either T-pose or crash? **No, and here is the chain that prevents it.**

`FFX_Ch_Allocate 0x824F90` (RVA `0x00424F90`) seeds the locomotion slots as

```c
FFX_Ch_SetSlot(chr, 0, 0);    // idle
FFX_Ch_SetSlot(chr, 1, 1);    // walk
FFX_Ch_SetSlot(chr, 2, 2);    // run
```

Those are **logical indices, not motion ids.** A slot value below `0x1000` is resolved
through `FFX_Mot_SetByModeIndex 0x837D00`, which reads
`*(CHRDATA.m_blobBase + 16 + 8 * g_ffxMotModeToSetSlot[mode])`, that is, **the model's
own `.chr` section 5 table.** The CHRDATA came from the model's own `.chr`. So slot 0
means "whatever this model calls idle", and a swapped model animates with its own clips.

The rest of `FFX_Ch_Allocate` is the same story, all data driven off the id:

- `FFX_Ch_BindChrData 0x826070` copies `CHRDATA.m_id` into `CHR+0x00` and
  `CHRDATA.m_name` into `CHR+0x04`, then rebuilds the per-part mesh array from
  `CHRDATA.m_partTable`. The CHR's own idea of what it is comes from the data.
- `FFX_Ch_BuildSkeletonInstance 0x8277F0` allocates the joint array from **that model's**
  skeleton header, so the joint count is per model and per allocation.
- `FFX_Ch_SetScaleUniform(chr, ChrData->m_defaultScale)` takes the scale from the model's
  own section 9 parameter block, so swapping in a large monster is correctly sized with
  no manual scale.
- `FFX_Chr_AttachModelInstance 0x63D370` (RVA `0x0023D370`) is handed
  `FFX_ChCache_GetModel(1, chrId)`, so the Phyre mesh is the one for that id.
- Even the hardcoded per-model quirks are id-keyed table lookups inside
  `FFX_Ch_Allocate`: a 49-entry id table at VA `0xC494D8` sets `m_flags2` bit 2, an
  id-indexed float pair table from VA `0xC495B0` writes `CHR+0x814` and `CHR+0x818`, and
  explicit id lists set `m_flags2` bits `0x80` and `0x200`. All of that fires correctly
  for a swapped model because it is driven by the id the plugin passed.

### The one-instance cache question, answered

`PHASE1_CLONE.md` is right that `FFX_Ch_LoadChrData` short-circuits, and the detail
matters for the opposite reason to the one you would expect. The first line of
`FFX_Ch_LoadChrData 0x825A40` is

```c
result = FFX_Ch_FindChrData(chrId);
if (result != nullptr)
    return result;
```

**That cache is keyed on chrId.** So it is not a one-instance cache that would make a
second allocation hand back the wrong model. Two allocations of the SAME id share one
read-only CHRDATA, which is the designed path and is what makes a clone free. Two
allocations of DIFFERENT ids get different CHRDATA and cannot interfere. **A model swap
is therefore unaffected by the cache.** The only thing it changes is that the first
allocation of a new id pays the load and later ones do not.

### CORRECTION, 2026-10-04, read this before using the sequence below

Two parts of the sequence that follows are unnecessary and one note in it is wrong. See
reversing/CHR_RESIDENCY.md, which traced the loader to the bottom.

* **Drop the `FFX_Ch_RomRead` plus `FFX_Ch_DataReadSync == 1` retry.** `FFX_Ch_LoadChrData`,
  which `FFX_Ch_Allocate` calls, already creates the cache entry, starts the read and blocks
  on `FFX_Ch_RomPump(0)` until the read and its completion callback are done, inside the
  calling frame. That is why `FFX_Ch_DebugSpawnByName` does nothing about residency at all.
* **Drop the `MotionSetReadStart` then poll `MotionSetReadSync` pair.**
  `FFX_Ch_LoadMotionSetSync` pumps its own read the same way.
* **"2 = no cache entry, pops an error box" is wrong.** `yiAssert` reduces to one `vprintf`
  to stdout in retail. It is not modal, not fatal and costs no frame.
* **One check is MISSING and it is a crash.** The CHRDATA table at RVA `0x01FC8D00` is 40
  records of 300 bytes, free when the first dword is -1. `FFX_Ch_BlkAllocate` walks for a
  free one and, finding none, leaves its result pointer null and memsets 300 bytes through
  it. Count free records before allocating, the same way the CHR pool is counted.
* **Prefer `FFX_Ch_SetNoFallbackOnce` RVA `0x004285D0` to the `m_isFallback` check.** One
  instruction, one shot, and it makes the next `FFX_Ch_Allocate` return null instead of
  silently substituting `c001`.

The `g_ffxTidusChr` ordering, the per-category motion mode and the walkmesh bind below are
all unaffected and all still required.

### The call sequence

```c
// Read these BEFORE allocating anything.
CHR* oldTidus  = *(CHR**)(base + 0x00EFBC60);        // Rva::TidusChr
int  partyIdx  = oldTidus->m_partyIndex;
char b184      = oldTidus->m_b184;
float x, y, z, yaw;
FFX_Ch_GetPos(oldTidus, &x, &y, &z);                 // RVA 0x0042AC90
yaw = oldTidus->m_rotY;

// Pool headroom. FFX_Ch_Allocate NULL-DEREFS when the pool is full: with no free
// slot its search variable stays null and it then memsets 0x880 bytes through it.
// Verified in the disassembly, the slot search falls out of its loop unassigned.
if (FFX_Ch_CountLive() >= *(int*)(base + 0x01FC44E0))
    return;                                          // Rva::ChrCount

// Page the new model in. All four are idempotent.
FFX_Ch_RomRead(newId);                               // RVA 0x00429EF0
if (FFX_Ch_DataReadSync(newId) == 1) return;         // 1 = still loading, retry next frame
                                                     // 2 = no cache entry, pops an error box
int mode = (cat == 1 || cat == 3) ? 1 : 0;           // see the mode note below
FFX_Ch_MotionSetReadStart(newId, mode);              // RVA 0x00436A40
if (FFX_Ch_MotionSetReadSync(newId, mode) == 1) return;  // RVA 0x00436A50

CHR* neo = FFX_Ch_Allocate(newId);                   // RVA 0x00424F90
if (!neo) return;
FFX_Ch_LoadMotionSetSync(newId, mode);               // RVA 0x00436870

// Detect the silent fallback described in section 2. If the model did not exist,
// this is a second Tidus and not the model that was asked for.
if (neo->m_data->m_isFallback) { FFX_Ch_DisposeIfLive(neo); return; }

// Repoint the one-slot main-character cache BEFORE anything can dispose the old
// body. See the fixups below, this is the line that stops the dispose from
// nulling the player binding.
*(CHR**)(base + 0x00EFBC60) = neo;

FFX_Ch_SetByte184(neo, b184);                        // RVA 0x00435B50
FFX_Ch_SetPartyIndex(neo, partyIdx);                 // RVA 0x0042B0D0
FFX_Ch_SetPos(neo, x, y, z);                         // RVA 0x0042B480, sets m_walkmeshTri = -1
FFX_Ch_SetRotAndMoveDir(neo, yaw);                   // RVA 0x0042B1B0, both fields at once

// Monsters and aeons need the mode 1 locomotion setup, copied from
// FFX_Ch_DebugSpawnByName. Skip for c, w, f and k.
if (cat == 1 || cat == 3)
{
    FFX_Ch_SetLocomotionMode(neo, 1);
    FFX_Mot_SetByModeIndex(neo, 1, 16);
    FFX_Mot_SetPendingLoopCount(neo, 0);
}

// The bind PHASE1_CLONE.md proved is required or the character is invisible
// forever. Ground mode 1 writes m_posY = m_groundHeight every frame and
// m_groundHeight is 0 until something binds a triangle.
neo->m_velX = neo->m_velZ = 0.0f;
FFX_Ch_WalkmeshMove(neo);                            // RVA 0x0043E5F0

// Move the player binding, then retire the old body.
FFX_Ch_SetPlayerChr(neo);                            // RVA 0x0042DAD0
FFX_Ch_DisposeIfLive(oldTidus);                      // RVA 0x004722F0
```

### The state fixups, and the one that bites

**`g_ffxTidusChr` RVA `0x00EFBC60` is the hazard, and the order above is deliberate.**
`FFX_Ch_BindChrData` writes that global unconditionally for any CHR whose
`CHRDATA.m_name` is `"c001"` or `"c101"`, and it is a one-slot cache with no guard. Two
consequences for a swap:

1. **Swapping to any model other than `c001` or `c101` leaves `g_ffxTidusChr` pointing at
   the OLD body.** Then `FFX_Ch_Dispose` on that old body runs its guard,
   `if (GetPlayerChr() == chr || g_ffxTidusChr == chr) { SetPlayerChr(null);
   g_ffxTidusChr = null; }`, and nulls the player binding you just set. **That is why the
   global has to be repointed before the dispose, and why the dispose comes last.**
2. Swapping to `c101` repoints it for free, because `BindChrData` treats `c101`
   identically to `c001`.

The other reader of that global, `FFX_Ch_StepAll` at VA `0x82EE60`, only publishes that
CHR's position and is cosmetic.

### What still breaks, honestly

The model and its animation are correct. These are the things that are not, and they are
all consequences of a character's identity being used for more than its mesh:

- **The ATEL event script does not know.** A script that addresses the player actor
  addresses the actor, and the actor's model is set by `FFX_AtelOp_SetActorModel
  0x85D000` from `actor+168`. **So the next cutscene or map transition puts Tidus's model
  back.** A swap applied from the `animate` hook therefore has to be re-applied, and the
  natural place is the boot-task hook `PHASE1_CLONE.md` recommends,
  `FFX_BootTask_Push(0, myTask, "COOP")`. State this in the UI as "lasts until the next
  map".
- **No motion mismatch, but there IS a mode mismatch for monsters and aeons.** This is
  the real catch and it is visible in `FFX_Ch_DebugSpawnByName`, which branches on the
  category:

  ```c
  if (cat == 1 || cat == 3) {              // mon, sum
      FFX_Ch_LoadMotionSetSync(id, 1);     // mode 1, FIELD BATTLE, not field
      FFX_Ch_SetLocomotionMode(chr, 1);
      FFX_Mot_SetByModeIndex(chr, 1, 16);
      FFX_Mot_SetPendingLoopCount(chr, 0);
  } else if (cat == 2) {                   // npc
      // a separate /ffx/npcanm/<name>.anm file, NOT an .mgrp at all
  } else {                                 // c, w, f, k
      FFX_Ch_LoadMotionSetSync(id, 0);     // mode 0, field
  }
  ```

  So **monsters and aeons keep their animations in mode 1, not mode 0.** That explains
  the shape of `tools/pcmodels.py` output exactly: almost every monster shows 0 field
  motions and 150 to 240 fieldbtl motions. `pcmodels.py`'s "safe to spawn" test is
  `section 5 count > 0`, which is the right test for a `c`-series model and **the wrong
  test for a monster or an aeon.** 349 of 859 pass it, but far more than 349 models are
  usable, because the mon and sum categories just need the mode 1 path. **The plugin
  should mirror the category branch above rather than filter on mode 0.** This is the
  single most likely thing to get wrong.
- **NPCs are a third animation system.** Category 2 loads `/ffx/npcanm/<name>.anm`
  through `FFX_Ch_ReadFileDev`, stores it into CHRDATA, and either sets
  `m_flags1 |= 0x80000000` with `m_playSpeed = 256` for the short form or calls
  `FFX_Ch_SetMotionData` for the long form. A plugin that wants NPC models has to copy
  that branch. Note it writes into the SHARED CHRDATA, so it is one-per-model state, not
  per-CHR.
- **Skeleton mismatch cannot happen, so it neither crashes nor looks wrong.** The
  skeleton is built per allocation from the model's own header and the clips come from
  the same model, so there is no path by which a clip indexes a joint the model does not
  have. The failure the brief worried about is prevented structurally rather than by a
  check.
- **The walkmesh and camera consequences from `PHASE1_CLONE.md` all still apply**, and
  they apply to the NEW body, because they are about which CHR the player global points
  at. After `FFX_Ch_SetPlayerChr(neo)` it points at the new one, so the camera follows,
  noclip applies, and player-only walkmesh surfaces work. That is the other reason to
  move the player binding rather than leave it on a corpse.

### Why not (b) or (c)

- **(b) swap the model instance on the live CHR via `FFX_Chr_AttachModelInstance`.**
  Looks surgical, is not. That function only fills `CHR+0x830` (and `+0x838`), either
  immediately through `FFX_Chr_CreateInstanceNow` or by queueing a 16-byte deferred
  record `{outSlot, ClassCharacter*, CHR*, callback}` onto a pending list. It does not
  touch `CHR+0x00` (the id), `CHR+0x04` (the name), `CHR+0x1C0` (the part table),
  `CHR+0x1C4` (`m_data`), the joint array, the channel array, the scale or the motion
  slots. **So you get a new mesh driven by the old skeleton and the old model's clips,
  which is precisely the retargeting disaster option (a) avoids**, and you would have to
  hand-redo everything `FFX_Ch_BindChrData` and `FFX_Ch_BuildSkeletonInstance` do.
  Strictly more work for a worse result.
- **(c) dispose then re-allocate.** This is option (a) with the order reversed, and the
  reversal is the problem: disposing first runs the `g_ffxTidusChr` guard while the
  global still points at the body being destroyed, which nulls `g_ffxControlledChr`, and
  it frees the pool slot before you know the new model will load. Allocate first, repoint
  the global, move the binding, then dispose. Same calls, safe order.

---

## 4. The party

### THE LIST: 18 characters, from a table in the exe

`CharIndexToChrId` RVA `0x008423A0` (VA `0xC423A0`) is a **table, not a function**:
`DWORD[18]`, stride 4, 72 bytes. Verified by reading the raw bytes, and the item at
`+72` is `0x8000FFFF` followed by `0xC0002D2D`, plainly a different structure, so the
count is exactly 18.

| idx | chrId | model | `DebugCharNames[idx]` | character |
|---|---|---|---|---|
| 0 | `0x0001` | `c001` | `Tidus(0)` | Tidus |
| 1 | `0x0002` | `c002` | `Yuna(1)` | Yuna |
| 2 | `0x0003` | `c003` | `Auron(2)` | Auron |
| 3 | `0x0004` | `c004` | `Kimari(3)` | Kimahri |
| 4 | `0x0005` | `c005` | `Wakka(4)` | Wakka |
| 5 | `0x0006` | `c006` | `Lulu(5)` | Lulu |
| 6 | `0x0007` | `c007` | `Rikk(6)` | Rikku |
| 7 | `0x0008` | `c008` | `Seymour(7)` | Seymour |
| 8 | `0x3001` | `s001` | - | Valefor |
| 9 | `0x3002` | `s002` | - | Ifrit |
| 10 | `0x3003` | `s003` | - | Ixion |
| 11 | `0x3004` | `s004` | - | Shiva |
| 12 | `0x3006` | `s006` | - | Bahamut |
| 13 | `0x3007` | `s007` | - | Anima |
| 14 | `0x3008` | `s008` | - | Yojimbo |
| 15 | `0x3009` | `s009` | - | Cindy |
| 16 | `0x300A` | `s010` | - | Sandy |
| 17 | `0x300B` | `s011` | - | Mindy |

`0x3005` really is skipped. Rikku has a runtime override: `FFX_Btl_ResolveUnitChrId`
RVA `0x0039A440` substitutes chrId **41** (`c041`) when `saveData+0xD1` is non-zero,
through `FFX_SaveData_GetRikkuAltOutfit` RVA `0x0046A7E0`. That is the alternate outfit,
and it is why `c041` borrows all four of `c007`'s motion banks, which `PHASE1_CLONE.md`
noticed without knowing why.

**`DebugCharNames` RVA `0x0083432C` is `char *[8]`, stride 4, count 8.** Verified: the 8
pointers go into `.rdata` at `0xB48660` onward and entry `[8]` is `??_7type_info@@6B@`,
so the array genuinely stops at 8. The strings carry the index in parentheses and use
the developers' spellings, `Kimari` and `Rikk`.

**What the picker should actually display.** Not `DebugCharNames`, because it covers only
8 of 18 and is not localised. Use **`FFX_GetUnitDisplayName` RVA `0x004AC850`**, which
for an index below 20 forwards to `FFX_SaveData_GetCharName` RVA `0x00384FB0` and returns
the localised name from `CharNames` RVA `0x00D32DDC` (18 rows of 20 bytes). That covers
all 18 including the aeons.

**One hazard there, and the comment in `GameState.h` is misleading about it.**
`FFX_SaveData_GetCharName`'s guard is `FFX_Btl_IsAllyUnit`, which is `index <= 30`, not
`index <= 17`. So `GetCharName(18)` returns `&CharNames[360]`, which is the save-block
CRC field at RVA `0x00D32F44`, as a string. **The UI has to bound to 0..17 itself.** That
same arithmetic is what proves the row count: the CRC field is exactly 18 rows past the
base.

So the character picker is fully runtime-enumerable in process with no baked data: walk
`CharIndexToChrId[0..17]`, take the chrId for the model link, take the label from
`FFX_GetUnitDisplayName(i)`, and cross-check against `DebugCharNames` for the first 8
during development.

### How membership is stored: a 20-byte order array plus a per-record flag

`GameState.h` undersells this. `saveData+0x3D58` is **one contiguous 20-byte array** of
character record indices, `0xFF` meaning empty:

- `[0..2]` the three **active** field party slots. RVA `0x00D307E8`.
  `SaveDataGetFieldPartyArray 0x00385270` returns this and writes 3 to its out-count.
- `[3..19]` the 17 **bench** slots. RVA `0x00D307EB`.
  `FFX_SaveData_GetPartyBenchArray` RVA `0x003852D0` returns this and writes 17.

That the two are one array is not a guess: `FFX_SaveData_SetCharInParty` scans the array
for **20** entries, and the shipped party menu's commit passes `3 + benchPosition` as a
slot index into it.

**Three field slots. Three battle slots. Not more.** That is the hard limit and it is not
a soft one, it is the array size.

Separately, membership is **bit 0 of `record[0x2C]`**, record stride 148, records base RVA
`0x00D3205C`, so the flags byte for index `i` is at RVA `0x00D32088 + 148*i`. Confirmed.
There is **no separate membership bitset**, the ordered array and the per-record flag are
two independent facts and both matter. The other bits on that byte: 1 permanent, **2
locked into its slot**, 4 selectable.

**Bit 2 is the thing most likely to make a cheat write silently do nothing.**
`FFX_SaveData_SetPartyOrderSlot` refuses the swap when either the incoming character or
the current occupant has it set. That is the story-forced-member lock.

### Battle party order

`BattlePartyOrder` RVA `0x00D2C895` is **`BYTE[7]`** of character record indices, not unit
indices (for allies the two coincide, since actor index equals record index for 0..17).
`[0..2]` are the active battle slots and `[3..6]` are memset to `0xFF` by setup. Its
complement is at RVA `0x00D2C8A3`, `BYTE[17]`, which the IDB calls
`g_ffxBattleAeonOrder`. **That name is wrong, it is the battle BENCH**, and
`FFX_Btl_SwapUnitIntoPartySlot` keeps the two as an exact partition. Recorded as an
appended comment in the IDB rather than by renaming someone else's symbol.

- **`BtlSetupUnitRoster 0x0039C110` reads** `saveData+0x3D58[0..2]`, `saveData+0x3D5B[17]`
  and `IsCharInParty(0..17)`. It **writes** per active slot `actor+16 = 1` (exists),
  `actor+3528 = 1` (front row) and `actor+1278 = slot`, then `BattlePartyOrder[0..2]`,
  `memset(BattlePartyOrder+3, 0xFF, 4)` and the bench copy, then marks everyone with the
  in-party flag, resolves chr ids and loads params for all 18, then builds the 8 enemy
  units. **One caller**, `FFX_Btl_SetupFieldPointers 0x00383E20`, at battle load.
- **`BtlCommitPartyToField 0x00386930` reads** `BattlePartyOrder[0..2]` and **writes**
  `saveData+0x3D58[0..2]`. A raw 3-byte copy with no filtering. It runs at battle end
  from `FFX_Btl_CommitActorsToSave 0x00385FC0`. The other call site, in `FFX_Btl_Init`,
  is behind a debug-mode branch and is not the retail path.

So the direction of travel is: field party to battle at load, battle to field party at
end. **Neither is called on demand, and that is what makes the timing rule below.**

### The write sequence

```c
// 0. Clamp idx to 0..17 YOURSELF. See the missing range checks below.
//    Make each wanted member a party member first: SetPartyOrderSlot refuses a
//    character who is neither already in the order array nor IsCharInParty.
SaveDataSetCharInParty(idx, 1);    // RVA 0x003869B0. Pass literally 1, never 2.

// 1. Optional, and needed for story-locked members: clear record[0x2C] bit 2 at
//    RVA 0x00D32088 + 148*idx. There is no setter, poke the byte.

// 2. Set the three active slots with the engine's own setter, which keeps the
//    bench consistent and does the swap bookkeeping.
FFX_SaveData_SetFieldParty3(a, b, c);   // RVA 0x00386950
// Verified to be exactly SetPartyOrderSlot(a,0), (b,1), (c,2).
// a/b/c are character record indices, 0xFF for an empty slot.
// Each returns -1 applied, 0 REFUSED. Check them, a refusal is silent otherwise.

// 3. Make it visible on the current map. Writing the bytes is NOT enough, see below.
FFX_Atel_BuildPartyVisibleMask();       // RVA 0x004623B0
```

**Do NOT call `BtlCommitPartyToField` or `BtlSetupUnitRoster` after a field party
change.** That is the direct answer to the question in the brief, and both would be
wrong:

- `BtlCommitPartyToField` copies **battle to field**, so calling it after setting the
  field party overwrites what you just wrote with whatever stale bytes are in
  `BattlePartyOrder`. It is the exact opposite of what the name suggests you want.
- `BtlSetupUnitRoster` copies **field to battle**, so it is the right direction, but it
  also builds the 8 enemy units and writes 18 actors' worth of battle work RAM. Calling
  it outside a battle load writes into actor memory that is not set up. It runs by itself
  at the next battle load, which is when it is wanted.

Set the field party and let the engine propagate it. Nothing needs calling.

### Timing, and the clobber trap

- **On the field**: the save block and the next battle are both correct from then on. The
  visible characters do not change until the field boot chain re-runs, see below.
- **While a battle is running**: nothing happens in that battle, and at battle end
  `FFX_Btl_CommitActorsToSave -> BtlCommitPartyToField` **overwrites your three bytes**
  with `BattlePartyOrder[0..2]`. The change is silently lost. This is the falsifiability
  trap: the engine clobbers the write, so a probe that only reads back before the battle
  ends looks like a success.
- **To change the party mid-battle**, use `FFX_Btl_SwapUnitIntoPartySlot` RVA
  `0x003ADAE0`, signature `int (u8 outIdx, u8 inIdx, int mode, char)`, mode 0 for a plain
  Switch and mode above 0 for the Summon path. A raw write to `BattlePartyOrder` is **not
  sufficient**: the engine also needs `actor+16`, `actor+3528`, `actor+1278`, the CTB turn
  queue entries removed and re-inserted, and the bench kept complementary. That function
  does all of it.

### A party change does not change the visible characters

Settled, and the nuance matters:

- **Which CHRs exist is map data, not party data.** `FFX_Field_ChrRegTask 0x00461850`
  (the `"CHRREG"` boot task) walks every ATEL actor in the loaded map and spawns a CHR
  from the chr id at `actor+168`, which comes from the map's authoring. It then calls
  `FFX_Ch_SetPartyIndex(chr, actor+0x40 == -1 ? 255 : actor+0x40)`.
- **Which CHRs are VISIBLE is the party.** `FFX_Atel_BuildPartyVisibleMask 0x004623B0`
  turns the three active slots into a bitmask at RVA `0x00F26B7C`, and
  `FFX_Atel_RestoreActorChrState 0x0046E5B0` calls `FFX_Ch_SetHideBit1 0x0042B2C0` per
  actor from that bit, for `actor+0x40 < 8` only.
- **The mask is rebuilt once per field boot.** `BuildPartyVisibleMask` has exactly one
  caller and it is the first line of `maybe_FFX_Field_QueueBootTasks 0x00461160`.
  `RestoreActorChrState` has one caller, which has one caller, CHRREG case 12. There is
  no per-frame re-evaluation.

So **yes, a party change needs the field boot chain to re-run before it shows.** Its
three natural triggers are a map change, a save load, and returning from battle. To force
it without a map change:

```c
maybe_FFX_Atel_ReleaseAllActorChrs();                     // RVA 0x00465DC0
maybe_FFX_Field_QueueBootTasks(1, mapId, sceneId, mode);  // RVA 0x00461160
```

The lighter alternative, which avoids a reload: call `BuildPartyVisibleMask`, then walk
ATEL actors with `FFX_Atel_GetActor 0x0046A830`, read `actor+0x40`, and call
`FFX_Ch_SetHideBit1` yourself.

### Seymour, the aeons, and Jecht

**Seymour (index 7) is fully supported in the engine and deliberately hidden in the UI.**
`c008` exists, chrId 8 is in the table, and both `SetCharInParty(7, 1)` and
`SetPartyOrderSlot(7, 0..2)` accept him with no index check. The engine's own script
auto-fill loops `idx <= 7`, so it considers him eligible. What excludes him is four
separate cosmetic exclusions: the shipped party menu skips index 7 in all three list
passes, `FFX_Btl_LoadPlayerData 0x003848B0` skips index 7 for the default ability mask,
`FFX_SaveData_LockEquippedGear` bounds `idx < 7` so his equipment never locks, and the
ATEL party snapshot's membership mask covers only 0..6 so his membership does not survive
a push and pop. **Verdict: putting Seymour in the party works.** He has no default
abilities beyond his own record bitmap and will not appear in the stock menu.

**The aeons (indices 8..17) work in battle and do not work on the field.**

- Battle: works. `FFX_Btl_SwapUnitIntoPartySlot` with `mode > 0` is the summon, and it
  writes the aeon's index straight into `BattlePartyOrder[0..2]`. `FFX_Battle_GetActor`
  accepts 0..30, so actors 8..17 are real ally actors.
- Field: refused, in three independent places, and **all three are soft. Nothing
  crashes.**
  1. `FFX_SaveData_SetPartyOrderSlot` **actively scrubs** every bench entry `>= 8` to
     `0xFF` on entry, and reads an active-slot occupant `>= 8` as `0xFF`. Verified in the
     pseudocode. The engine says no, explicitly.
  2. `FFX_Atel_RestoreActorChrState` only applies visibility for `actor+0x40 < 8`, so an
     aeon's bit can never match an actor.
  3. `FFX_Atel_FindActorByPartyChar` finds nothing, so `FFX_Atel_GetPartyMemberActor`
     returns -1, and every call site sign-tests and skips the opcode. `FFX_Atel_GetActor`
     clamps a negative index to 0, so there is no null deref. A "party member N, walk
     here" command silently does nothing, which in practice is a cutscene that never
     finishes.

  There is no field motion set problem to find, because no field spawn is ever attempted.
  CHRDATA for `s001` to `s011` does exist, so a mod could spawn an aeon CHR by hand
  through section 3's path, and per section 3 it would need the **mode 1** motion setup
  because `sum` is category 3.

**There is no Jecht character index.** The 18 records are 8 characters plus 10 aeons, full
stop. Jecht exists only as a monster or NPC chr id, which means the way to "play as
Jecht" is the model swap in section 3, not the party.

### The missing range checks, which a cheat UI must supply

All three read out of the instruction stream rather than from the decompiler's guess:

- **`SaveDataSetCharInParty 0x003869B0` has no bound at all.** The entry is
  `and ebx, 0FFh` then `imul ecx, 94h`. Index above 17 writes
  `0x00D32088 + 148*idx`, outside the 18 records. Index 255 lands about 37 KB past the
  array, in the middle of the save block. **Clamp to 0..17.**
- **Second trap on the same function**: the flags write is reached whenever `on != 0`, and
  it sets bits 0 and 4 to `on & 1`. So `SetCharInParty(i, 2)` **clears** membership while
  skipping the `IsCharPermanent` guard the `on == 0` path honours. **Pass exactly 0 or
  1.**
- **`FFX_SaveData_SetPartyOrderSlot 0x00384D00` has no upper bound on `slot`**, only
  `if (slot >= 0)`. `slot >= 20` writes past the array into the rest of the save block.
  **Clamp to 0..19.**
- `FFX_SaveData_GetCharRecord 0x00385330` is the only accessor in the family that does
  bound, with `index <= 0x11`.

### These belong in addresses/GameState.h

The party constants below are new and are **not** added to a header by this pass, because
`GameState.h` is owned by someone else this run. Whoever owns it should add them, and the
names are the ones used above.

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| `0x00384D00` | func | `SaveDataSetPartyOrderSlot` | `int (int charIndex, int slot)` | THE party setter. -1 ok, 0 refused. No upper bound on slot |
| `0x00386950` | func | `SaveDataSetFieldParty3` | `int (int s0, int s1, int s2)` | the engine's own 3-slot setter |
| `0x003852D0` | func | `SaveDataGetPartyBenchArray` | `u8 *(int *outCount)` | returns `0x00D307EB`, writes 17 |
| `0x00385360` | func | `SaveDataGetCharFlagBit2` | `BOOL (u8)` | bit 2 = locked into its slot |
| `0x003853C0` | func | `SaveDataIsCharPermanent` | `BOOL (u8)` | bit 1 |
| `0x003853A0` | func | `SaveDataIsCharSelectable` | `BOOL (u8)` | bit 4 |
| `0x00385330` | func | `SaveDataGetCharRecord` | `void *(int)` | the only bounded accessor, `<= 0x11` |
| `0x004AC850` | func | `GetUnitDisplayName` | `char *(int unitIndex)` | what the picker should display |
| `0x00387100` | func | `SaveDataReloadCharNames` | `int (void)` | fills all 18 rows from `ply_save` |
| `0x0039A440` | func | `BtlResolveUnitChrId` | `int (BattleActor *)` | the only reader of the chrId table |
| `0x0046A7E0` | func | `SaveDataGetRikkuAltOutfit` | `int (void)` | `saveData+0xD1`, swaps `c007` for `c041` |
| `0x00394020` | func | `BattleGetActor` | `void *(u8)` | `< 31` ally, 31..92 monster |
| `0x003ADAE0` | func | `BtlSwapUnitIntoPartySlot` | `int (u8 out, u8 in, int mode, char)` | the in-battle swap and summon |
| `0x003ADE10` | func | `BtlSwapPartyArrangement` | `void (void)` | push and pop the arrangement |
| `0x00385FC0` | func | `BtlCommitActorsToSave` | `char (void)` | calls CommitPartyToField at battle end |
| `0x00383E20` | func | `BtlSetupFieldPointers` | - | the only caller of `BtlSetupUnitRoster` |
| `0x004623B0` | func | `AtelBuildPartyVisibleMask` | `int (void)` | the only writer of the visible mask |
| `0x0046E5B0` | func | `AtelRestoreActorChrState` | `void (actor, int, int)` | applies the mask, `actor+0x40 < 8` |
| `0x00461850` | func | `FieldChrRegTask` | `int (DWORD *)` | the CHRREG boot task |
| `0x00461160` | func | `FieldQueueBootTasks` | `int (int, int, int, int)` | re-runs the field boot |
| `0x00465DC0` | func | `AtelReleaseAllActorChrs` | `void (void)` | drop every actor's CHR first |
| `0x0046A830` | func | `AtelGetActor` | `int (int)` | clamps a negative index to 0 |
| `0x0046A5E0` | func | `AtelGetPartyMemberActor` | `int (int slot)` | -1 when the slot is absent |
| `0x0046A470` | func | `AtelFindActorByPartyChar` | `int (int)` | scans actors for `+0x40 == idx` |
| `0x004A8F40` | func | `MenuBuildPartyOrderList` | `int (int flags)` | the shipped picker, skips Seymour |
| `0x004A9720` | func | `MenuCommitPartyOrderSwap` | `int (int a, int b)` | passes `3 + bench` as the slot |
| `0x0042B2C0` | func | `ChSetHideBit1` | `(Character *, int)` | the per-actor visibility lever |
| `0x00D307E8` | data | `FieldPartyActive` | `u8[3]` | `saveData+0x3D58`, `0xFF` empty |
| `0x00D307EB` | data | `FieldPartyBench` | `u8[17]` | contiguous with the above, one 20-byte array |
| `0x00D3205C` | data | `CharRecords` | `18 x 148` | the character records |
| `0x00D32088` | data | `CharRecordFlags` | `u8`, stride 148 | bit 0 in party, 1 permanent, 2 locked, 4 selectable |
| `0x00D32F44` | data | `SaveBlockCrc` | dword | the end bound that proves `CharNames` is 18 rows |
| `0x00D2C8A3` | data | `BattleBenchOrder` | `u8[17]` | the IDB calls this `g_ffxBattleAeonOrder`, which is wrong |
| `0x00F26B7C` | data | `AtelPartyVisibleMask` | dword bitmask | which of chars 0..7 are visible |

---

## 5. Launching a minigame

### The headline: the whole game is one launcher

There is no per-minigame native entry point for the field minigames, and looking for one
is the wrong shape. **Every map, every cutscene and every field minigame is an ATEL event
package identified by a single integer**, and that integer is the same number in three
places: the argument to `FFX_Ev_LoadEventPackage`, the map id `FFX_Map_RequestChange`
takes, and the row index of a shipped file that holds all 402 package names.

So the launcher is generic, the picker is data, and "start minigame X" is one call.

### THE LIST: 402 event packages, from a shipped file

**`ffx_ps2/ffx/proj/event/header/eventid.bin`, 6741 bytes.** Confirmed present in
`data/FFX_Data.vbf` and parsed byte for byte:

```
{ u32 nameOffset; u32 nameLen; } [402]   // at file offset 0
... NUL-terminated ASCII names from offset 3216 on ...
```

**The first dword does double duty and that is worth saying plainly, because getting it wrong
shifts every name by one.** It is both the record array's byte size (3216, so the count is
3216 / 8 = 402) and record 0's own name offset. So the records start at offset **0**, not at
+4. `FFX_LoadEventIdTable` settles it: it does `sprintf(row, &file[*(u32 *)&file[8 * i]])`,
index `8 * i` with no `+4`. Reading the records at +4 yields `(len[i], off[i+1])` pairs, which
looks perfectly plausible and silently gives every name shifted by one.

A shifted parse is detectable without re-reading the disassembly. Cross-check against the
engine's own asset path table, where 348 ids have a kind-12 path and 18 of those ship no file.
Under the correct parse all 18 fall inside the 24 named-but-absent ids below. Under the shifted
parse only 4 of 18 do.

**The event id is literally the record index.** 400 of the 402 rows have a name, 2 are
empty holes. Spot-checked ids that matter: 62 `bltz0000`, 140 `kami0000`, 223
`nagi0000`, 241 `mcfr0100`, 290 `lmyt0000`, 302 `swin0000`, 307 `nagi0700`, 347
`bltz0200`, 382 `hiku2100`.

In process the table is built by **`LoadEventIdTable` RVA `0x00507F50`** into 16-byte rows
at `*(void **)(base + 0x021D5888)`, with the count at `*(int *)(base + 0x01534EC8)` and a
one-shot loaded flag at `0x01534ECC`. A row is `char name[12]` then `int id` at `+12`.
**All of that is already declared in `addresses/WorldState.h`** as `LoadEventIdTable`,
`EventIdNameTable`, `EventIdNameCount`, `EventIdTableLoaded`, `EventIdNameRowStride` and
`EventIdNameMaxChars`. Use those constants, do not redeclare them.

**Can a plugin enumerate it at runtime in process? Yes, and this is the preferred
route**, with two caveats:

- The game itself only calls that loader in debug mode or from the AutoTest command, so
  **the table is empty until something calls it**. Both the count and the loaded flag
  initialise to **-1**, verified by reading the dwords, so the "not loaded" test is
  `count < 0` and not `count == 0`. A plugin can call the loader directly in a retail
  build: the read path resolves cleanly to the shipped file (`FFX_Ch_ReadFileDev` ->
  `Sg_PcRead` prepends `host0:` -> the next layer strips 7 characters and prepends
  `/ffx_ps2/`, which is exactly the archive path), and the existence check happens before
  any assert could fire. The one-shot flag makes a second call free. It leaks the
  6741-byte buffer once.
- `WorldState.h` also warns, correctly, that **one row has no NUL terminator**, so cap
  the name at 12 bytes when copying it out.
- Alternatively parse `eventid.bin` out of the VBF with `tools/vbf.py` and bake it. 402
  short strings is small. Given the loader is callable, prefer calling it.

**Building a label.** The names are terse 8-character codes and **the exe ships no
human-readable area or minigame names anywhere**. So the honest answer is: the LIST is
data, the LABELS are partly not. What you get for free is the 4-character area prefix,
and there are **66 distinct prefixes in the real data** (`azit bika bltz bvyt hiku kami
lmyt luca maca mcfr mihn nagi omeg swin` and so on). The MapSwitching debug page at RVA
`0x002B9E20` has 42 of them as string literals, which is a subset and not the whole set,
so do not treat that list as complete. `FFX_Map_GetCurrentMapName 0x00507E70` gives the
current package's name, which makes a "you are here" row free.

**Recommended picker shape**: enumerate the 402 ids at runtime, filter to the ones with a
shipped package, drop the `test*` / `samp*` / `scen*` / `open*` / `endg*` / `loop*`
groups, group by the 4-character prefix, and keep a small baked table of about 20
`{eventId -> "Chocobo race (Calm Lands)"}` rows for the minigames. The list is then game
data and only the friendly names are typed.

### THE SAFETY TRAP, and it is a hard hang

**`FFX_Ev_LoadEventPackage` RVA `0x00472EF0` validates the package with an infinite
loop, not a return.** Verified in the decompilation, verbatim:

```c
if ( *(BYTE *)v1 != 'E' || *(BYTE *)(v1 + 1) != 'V' )
{
    while ( 1 )
        ;
}
```

So **offering an id whose `.ebp` does not ship hangs the game dead**, with no crash dump
and no log.

**Measured directly against the archive rather than taken on trust: 376 of the 402 ids
have a shipped package.** 400 rows are named, 397 distinct `.ebp` basenames ship, and the
**24 named-but-absent ids** are 40, 186, 216, 228, 229, 231, 232, 233, 246, 251, 262,
300, 304, 342, 350, 353, 357, 358, 360, 369, 373, 379, 400, 401. Ids 101 and 111 are the
two unnamed rows. Ids 393 to 399 take a hardcoded `scene1` to `scene7` path and bypass
the id table, and **399 quits the field** rather than loading anything.

**A discrepancy worth reconciling, not papering over.** `addresses/WorldState.h` carries
`EventIdUsableCount = 330` described as "ids with a package that actually ships". The
measurement above gives 376 for that exact criterion. 330 is probably a stricter notion,
ids a player can actually reach, which would exclude the `test*`, `samp*`, `scen*` and
similar groups. Either way the two numbers answer different questions and the header's
wording does not say which. Left for that header's owner, and the number this document
relies on is the measured 376.

**The picker must filter against what actually ships.** The cheapest reliable filter is a
baked set of the 26 bad ids, because the plugin cannot see the archive directory in
process. Build that set once with `tools/vbf.py` and keep it. This is the one place in
this whole document where baking is not optional.

### The launcher

**`FFX_Map_RequestChange` RVA `0x0048EA60`, `void __cdecl (int eventId, char entryPoint)`.**
Already in `addresses/WorldState.h`. It is five stores and a flag, with **no gate of any
kind**:

```c
saveData.byte[0x0D] = saveData.byte[0x0C];   // previous entry point
saveData.word[0x02] = saveData.word[0x00];   // previous map id
saveData.word[0x00] = eventId;
saveData.byte[0x0C] = entryPoint;
g_ffxMapChangePending = 1;                   // RVA 0x00F3084C
```

Nothing loads in that call. `FFX_Atel_StepOnce 0x0048D3D0`, which runs inside
`FFX_MainStep`, sees the flag, counts the delay down and calls `FFX_Scene_Init(saveData,
*(u16 *)saveData)`. That is the whole mechanism, and it is why a deferred request is
safe.

Three special cases to respect: `eventId == 399` only sets a quit flag and returns,
`eventId == 23` (title) runs extra teardown, and a **negative** eventId means "use the
checkpoint", taking the map from `saveData.word[0xBA]`.

**Do NOT call `FFX_Map_WarpTo 0x0046FEC0`**, which `WorldState.h` already warns about. It
carries a self-consuming gate bit on the ATEL context and will silently do nothing.

### Running a specific script entry, which is what actually starts a field minigame

Warping to the map is usually not enough, because the minigame is an actor entry the NPC
dialogue normally fires. The primitive for that is **new and was not in `addresses/Atel.h`**:

| RVA | name | signature |
|---|---|---|
| `0x0046E990` | `FFX_Atel_StartThread` | `int (int callerActorId, int targetActorId, int kind, int channel, int entryId)` |
| `0x004766D0` | `FFX_Atel_FireActorEventWithEntry` | `int (int caller, int targetActor, int eventKind, int entryIdOrNegative)` |

`StartThread` has no dedupe, and `0xFFFF` as the caller means "no caller".
`FireActorEventWithEntry` is the nicer shipped helper: it keeps the engine's channel and
`actor+171` event-mask checks but lets you name the entry. An out-of-range entryPoint
falls back to entry record 0, so a bad entry is safe rather than fatal.

### Battles are better served

`FFX_Btl_RequestScriptedBattle` RVA `0x00381C90`, `int (int battleId, char a2, char a3)`,
where **`battleId = (mapId << 16) | encounterId`**. Verified: it is gated on
`g_ffxBattleDisabled != 1` and `FFX_Btl_GetPhase() == 0`, resolves the id through the
encounter table, and sets the pending kind to 2. Deferred, so it is the safe route.
`FFX_Btl_BeginBattle 0x00381020` is the immediate version, and
`FFX_Debug_BeginSelectedBattle 0x003C6D80` is a shipped one-call launcher driven by three
selector globals.

A decompiler artifact worth not tripping over: the `call __exit` inside
`FFX_Btl_BeginBattle` is a misnamed function, not process termination. Both live battle
paths go down that branch every battle.

### Per-minigame launch table

| minigame | event id / package | entered normally | direct launch | verdict |
|---|---|---|---|---|
| **Blitzball match** | 62 `bltz0000`, hub 347 `bltz0200` | any Save Sphere "Play blitzball" warps to 347 | `MapRequestChange(347, 0)`. Go to the hub, not to 62, the match needs the roster state 347 builds. `FFX_Blitz_DebugFullBlitz 0x003845B0` first for every tech | **direct** (to the hub) |
| Blitzball tutorial, team screens | 355, 356, 347, 212, 340, 354 | from 347 | `MapRequestChange(id, 0)` | direct |
| **Chocobo race, Calm Lands** | 223 `nagi0000` | walk the Calm Lands, talk to the trainer | `MapRequestChange(223, 0)` then `AtelStartThread(0xFFFF, 82, 0, 1, 7)`. Write `ChocoboGameDebugEnable 0x008CCAB8` first for `dbg_nagi0000` | map warp + script fire |
| **Remiem Temple chocobo race** | 290 `lmyt0000` | only by chocobo from the Calm Lands | `MapRequestChange(290, 0)`, then interact in game. Script entry not pinned | map warp |
| **Monster Arena** | 307 `nagi0700` | `nagi0000` -> 307 | `MapRequestChange(307, 0)` for the shop. **The 36 arena fights are direct**: `(601<<16)\|105`, `(603<<16)\|{76,77,79..83,92..99}`, `(604<<16)\|{0..18}` through `RequestScriptedBattle(id, 1, 0)`. `FFX_Debug_ToggleFullNagi0700 0x006431F0` unlocks everything | **direct for the fights** |
| Monster capture | n/a | any battle, Capture weapon, killing blow | Not a minigame, one branch in `BtlOnUnitDefeated 0x0038C740`. Already in `Minigames.h` | not launchable |
| **Thunder Plains lightning dodge** | 140 `kami0000` | stand on the Thunder Plains | `MapRequestChange(140, 0)`. **It starts itself**, the strike scheduler is actor 31 entry 1 and the dodge window actor 30 entry 1, both auto-started by the map | **direct** |
| Dodge-200 reward variant | 256 `kami0400` plus a flag | 200 consecutive dodges | Set `ThunderPlainTreasureEnable 0x01685BA4` **before** warping to 256. It is a package swap at load time, so the order matters | direct, flag first |
| **Butterfly catching, Macalania** | 241 `mcfr0100` | `mcfr0000` -> 241, talk to the spirit | `MapRequestChange(241, 0)`, body is actor 38 entry 9, timer actor 37 entry 6 (40 s) or 7 (30 s). Penalty battles are `(310<<16)\|{0..5}` | map warp + script fire |
| **Cactuar hunt, Bikanel** | 129, 136, 137, 138, 130 `bika*` | airship to Bikanel | `MapRequestChange(129, 0)` and walk. No clock, no RNG, no single start script | map warp |
| **Via Purifico** | 207 `bvyt0500`, 198 `bvyt0900` | story, the Bevelle trial | `MapRequestChange(207, 0)` | map warp |
| **Jecht Shot practice** | 302 `swin0000`, 94 `swin0200` | story, on deck | `MapRequestChange(302, 0)` then actor 32 entry 8 | map warp + script fire |
| **Airship searches** | 382 `hiku2100` | talk to Cid on the bridge | `MapRequestChange(382, 0)`. Confirmed from its own message table: "Search an area", "Free search: Explore Spira", "Located Baaj Temple / Omega Ruins / Besaid Falls" | **direct** |
| **Overdrive input minigames** | battle, not an event | the character's overdrive command | `FFX_BtlOd_StartMinigameForActor 0x003AFD30(actorIndex)` is the single entry point. Per character: Tidus `0x00498CA0`, Auron `0x00498AD0`, Lulu `0x00498BF0`, Wakka reels `0x00498DC0`. `FFX_Debug_SetOverdriveAlwaysFull 0x00390420(1)` keeps the gauge full. `OVERDRIVES.md` owns the detail | **direct** |
| Ronso Rage, Mix, Grand Summon | n/a | battle menus | **Not input minigames.** `FFX_BtlOd_MaybeStartMinigame 0x003AFC90` only passes ability kinds `0x20/0x25/0x30/0x40`, which is Tidus and Lulu. The rest go through magic DLLs | not minigames |
| Chocobo Eater, Mi'ihen | battle | story | An ordinary scripted battle, no minigame code | n/a |
| Luca locker rooms | 228-233, 72, 73 `lchb*` | story | Rooms with chests, not a minigame. `lchb1300` does carry an in-script "Select Event" picker, which is a cutscene replay list | n/a |
| Al Bhed primers, Sphere Break | n/a | n/a | No minigame. Primers are pickups, and "sphere break" appears in none of the 397 packages, it is an FFX-2 feature | does not exist |
| Mi'ihen hover race | n/a | n/a | Does not exist. The only race minigames are the two chocobo ones | does not exist |

### Launch sketches

```c
// base = GetModuleHandle(NULL), link imagebase 0x400000

// A. launch any event package. One call, no gate, consumed next sub-step.
typedef void (__cdecl *MapRequestChange_t)(int eventId, char entryPoint);
((MapRequestChange_t)(base + 0x0048EA60))(307, 0);   // Monster Arena

// B. if you need it this step with no delay, write the state yourself:
short* sd = FFX_GetSaveData();
sd[0] = (short)eventId;
*((char*)sd + 0x0C) = entryPoint;
*(int*)(base + 0x00F30850) = 0;   // MapChangeDelayFrames
*(int*)(base + 0x00F30854) = 0;   // MapChangeDelayFlag
*(int*)(base + 0x00F3084C) = 1;   // MapChangePending

// C. run a specific actor script entry once the package is loaded
typedef int (__cdecl *AtelStartThread_t)(int caller, int actor, int kind, int chan, int entry);
((AtelStartThread_t)(base + 0x0046E990))(0xFFFF, 82, 0, 1, 7);   // nagi0000 race clock

// D. start a specific battle (Monster Arena creature 0)
typedef int (__cdecl *BtlReqScripted_t)(int battleId, char a2, char a3);
((BtlReqScripted_t)(base + 0x00381C90))((604 << 16) | 0, 1, 0);
```

### Shipped debug launchers, and the honest verdict on them

**There is no shipped debug launcher that starts a field minigame.** That was hunted for
properly. What does ship:

- `FFX_Debug_BeginSelectedBattle 0x003C6D80`, battles, one call.
- `FFX_AutoTest_JumpMap 0x00508100`, already in `WorldState.h`. A substring match over
  the event id table then a warp. Reachable today over stdin as `EnableAutoTest` then
  `JumpMap <name>`, and `EnableAutoTest` is what calls `FFX_LoadEventIdTable`.
- The four toggles, now named: `FFX_Debug_ToggleFullNagi0700 0x006431F0` (the menu label
  says "Full Arena Localization" and it actually means the Monster Arena fully
  unlocked), `FFX_Debug_SetThunderPlainTreasureEnable 0x00643210`,
  `FFX_Debug_SetOverdriveAlwaysFull 0x00390420`, and
  `FFX_Debug_StepChocoboGameDebugEnable 0x002B6790` (which needs 4 presses, so just write
  the byte at `0x008CCAB8`).
- `FFX_Blitz_DebugFullBlitz 0x003845B0` fills the Blitzball save block. **It does not
  start a match.**

One dead end recorded so it is not re-run: the Basic Debug Information page parses
`/ffx/proj/event/Section_GoThrough.txt` into a "Game Section" jump list, and **that file
does not ship**, so the row is empty in retail.

---

## 6. Live minigame state

### How a plugin knows which minigame is running, first and unambiguously

**Read the NUL-terminated string at RVA `0x01FCBC60` (VA `0x23CBC60`),
`g_ffxCurrentEventName`, and match on the tail after the `/`.**

Verified: IDA reports the item size as **256**, so it is a `char[256]` buffer **in place,
not a pointer**. `FFX_Ev_LoadEventPackage` fills it with an inline byte-copy loop, then
does `strstr(name, "/event/obj/") + 11` and `*strrchr(tail, '/') = 0`, leaving
`"<2-letter group>/<package>"`.

| in-process value | minigame |
|---|---|
| `bl/bltz0000`, `bl/bltz0002` | Blitzball match |
| `bl/bltz0200`, `bl/bltz0201` | Blitzball team screens |
| `na/nagi0000`, or `dbg_nagi0000` | Calm Lands chocobo race |
| `ka/kami0000` | Thunder Plains lightning dodge |
| `mc/mcfr0100` | Macalania butterfly hunt |
| `sw/swin0000`, `sw/swin0200` | Jecht Shot practice |
| `bi/bika0000` to `bika0400` | Cactuar hunt |
| `bv/bvyt0500`, `bv/bvyt0900` | Via Purifico |

**Two caveats, both verified and both important:**

- **Nothing clears this buffer on map unload.** It goes stale, not empty. So it answers
  "what was loaded last", and the plugin has to pair it with "is the field live".
- **A battle does not reload the event package**, so during a random encounter the name
  still reads `na/nagi0000`. For battle minigames use the overdrive discriminator
  instead, below.

The group prefix convention (`na/`, `ka/`, `mc/`) comes from the archive layout rather
than from the exe, so **match on the tail, not on the whole string**, and it does not
matter whether the convention holds.

Companion discriminators, in order of usefulness:

| RVA | what | note |
|---|---|---|
| `0x01FCBC60` | `g_ffxCurrentEventName`, `char[256]` | **the primary.** Already named in the IDB |
| `0x00EFBC40` | current event id, `u32` | one writer (`FFX_Ev_LoadEventPackage`), 93 readers. Already `EvCurrentEventId` in `Minigames.h` |
| `0x00D2CA90` | **live map id**, `u16` at `SaveData+0x00` | `FFX_SaveData_GetMapId 0x0048D660` is `return *(u16 *)GetSaveData()` |
| `0x00F3F77C` | `BtlOdMinigamePhase`, already in `Battle.h` | 0 idle, 1 armed, 2 running. **The overdrive-minigame discriminator** |
| `0x01FCBD70` | `EvPackageBase` | non-null means a package is loaded |

**Correction to `addresses/GameState.h`**: it has `g_ffxCurrentMapId` at `SaveData+0xB8`
(RVA `0x00D2CB48`). That is the **checkpoint** location, high word map and low word entry
point. **The live map id is `SaveData+0x00`.**

### The live script work area, which is where most minigame state lives

**`g_ffxAtelScriptWork` RVA `0x00D2CC7C` (VA `0x112CC7C`), 0x2000 = 8192 bytes.** It is
`SaveData + 0x1EC`, and the arithmetic was checked four independent ways:

- `0x00D2CA90 + 0x1EC = 0x00D2CC7C` exactly.
- `0x00D2CC7C + 0x2000` lands exactly on the next symbol, `g_ffxSphereGridNodes`, which
  pins the size.
- `0x00D2CC7C + 0x1000 = 0x00D2DC7C`, which is the existing `BlitzSaveBlock` constant.
- `0x00D2CC7C + 0x1392 = 0x00D2E00E`, which is the existing `BlitzPlayerByteArray`
  constant.

**`ScriptWorkAreaBackup 0x00F2D570` is dead code in retail, and the warning currently in
`Minigames.h` about it is wrong.** Both accessors are only ever referenced from
`maybe_FFX_TkHarness_Init_DEAD`, which installs them as function pointers behind a
predicate that returns a constant 0, so the pointers stay NULL and nothing calls them.
Verified by enumerating the xrefs: exactly one each, both from that dead function. Also
note `SaveDataClearScriptWorkArea` wipes only the **first 4096** of the 8192 bytes and is
not on the map-load path, so class 0 is genuinely persistent during play.

**How bytecode addresses a variable.** A script var is **not** a raw offset into the 8 KB
block. The bytecode carries a variable index into an 8-byte-per-entry descriptor table at
`*(u32 *)(actorDef + 0x14)`, and the descriptor unpacks as

```
type   = desc >> 28            // 0 u8, 1 s8, 2 u16, 3 s16, 4 u32, 5 s32, 6 f32
class  = (desc >> 25) & 7      // which storage area
bit 24 = a separate flag, NOT part of the offset
offset = desc & 0xFFFFFF
count  = *(u16 *)(desc + 4)    // array length
```

In all five packages checked, every actor in a package shares one descriptor table, so
var N means the same thing package-wide. **So the editor is a typed variable browser over
8 KB plus per-minigame labels for the interesting slots**, which is a good shape for a
cheat UI.

**But three of the seven classes are not in that block, and that is where the live timers
are.** The runtime base formula:

```
pkgBase  = *(void**)(base + 0x01FCBD70)                       // EvPackageBase
atelBase = pkgBase + *(u32*)(pkgBase + 4)                     // that offset is 0x40 in all 397 files
class 0     = (base + 0x00D2CC7C) + off                       // persistent, in the save block
class 6     = atelBase + *(u32*)(atelBase + 0x20) + off       // ONE base per package
class 2/3/4 = atelBase + *(u32*)(actorDef + 0x28/0x2C/0x30) + off     // PER ACTOR
              actorDef   = atelBase + *(u32*)(atelBase + 0x38 + 4*actorIdx)
              actorCount = *(u16*)(atelBase + 0x34)
class 5     = livePoolActor + 0x48 + off                      // AtelActorPool 0x01FCBD78
```

**Hard rule: classes 2 through 6 live inside the `.ebp` image and the actor pool, both of
which `FFX_Ev_LoadEventPackage` frees and reallocates on EVERY map change. Re-resolve the
base every frame you touch it, and never cache a pointer across a load.** The verified
`.ebp` magic check makes that failure loud at load time but a stale pointer is silent.

One extra hazard specific to class 3: the resolver tail-calls
`*(fnptr *)(g_ffxAtelCtx + 0x54)` when that slot is non-null, in which case the base
formula above does not apply. It is null in a normal field session, but check it.

### THE LIVE STATE TABLE

Discriminator = what `g_ffxCurrentEventName` holds. "cls" is the ATEL storage class, so a
`cls 0` row is a fixed RVA and a `cls 3` or `cls 6` row needs the formula above.

#### Blitzball, `bl/bltz0000`

| field | cls | address | width | why edit it |
|---|---|---|---|---|
| **home score** | 0 | RVA `0x00D2E0CE` | u8 | set the scoreline. In the save block, so it sticks |
| **away score** | 0 | RVA `0x00D2E0CF` | u8 | same |
| half or period flag (GUESS) | 0 | RVA `0x00D2E0D0` | u8 | name is inferred from a correlation only |
| **match clock, seconds elapsed** | 6 | class-6 base `+0x144` | s32 | the displayed MM:SS. Set to `halfLength - 1` to blow the whistle |
| **half length, seconds** | 6 | class-6 base `+0x148` | s32 | ships as 300, which is 5:00 |
| who just scored, 1 home 2 away | 6 | class-6 base `+0x128F` | u8 | a useful goal event hook |
| player-controlled actor id | - | `*(u16 *)(*(void **)(base + 0x00F26B28) + 10)` | u16 | which blitzer you are |

The two scores are **verified, not inferred**: the match controller increments one var at
six goal sites and the other at six more, each followed by setting the "who scored"
marker, and the descriptors resolve to class-0 offsets `0x1452` and `0x1453`. The clock
is verified from its own digit decomposition: the clock actor renders `var/600`,
`(var%600)/60`, `(var%60)/10`, `var%10`, a mixed radix of 600/60/10/1, which is MM:SS at
60 units per minute, so **the unit is seconds**.

**The cheap lever, and it is the recommended one: write 1 to `BlitzCheatEnabled` RVA
`0x008CCACC`.** The match script then polls the pad:

| combo | effect |
|---|---|
| L1 + Up | home score +1 |
| L1 + Down | away score +1 |
| L1 + Left | reset the match clock |
| L1 + Right | end the half |

**Prefer this to poking the package image**, because it is an ordinary ATEL write and
cannot desync the script interpreter.

**The persistent Blitzball block**, `BlitzSaveBlock 0x00D2DC7C`, 2560 bytes, is class-0
offsets `0x1000` to `0x1A00`. The complete tiling was recovered as the union of the
class-0 descriptors of all nine shipped `bltz*` packages, and it tiles end to end with
one 8-byte hole at `0x144A`. The two tech masks at `0x1000` and `0x10F0` (`s32[60]` each)
and the player level array at `0x1392` (`u8[60]`) are verified by what
`FFX_Blitz_DebugFullBlitz` writes. **Everything else in that block is a dimension, not a
meaning.** 60 is the player roster, 16 a team slot list, 6 the teams. **Do not surface
the unnamed fields in an editable UI.** All 2560 bytes are serialised into the save file,
so a wrong write there follows the player home.

Live match state that is **not** in that block, namely ball position, momentum, per-player
fatigue and AI scratch, is all class 3 (294 descriptors) and class 6 (269 descriptors), so
all of it is in the reallocated package image.

#### Chocobo race, `na/nagi0000`

All class 6 unless marked. The clock counts **up** with a 2:00 hard stop.

| field | address | width | why edit it |
|---|---|---|---|
| clock tenths / seconds / minutes | c6 `+0xB0` / `+0xB1` / `+0xB2` | u8 each | the live stopwatch. Freeze or rewind |
| **which course, 1..4** | c6 `+0xBC` | u8 | picks the result branch |
| **race state** | c6 `+0xDF` | u8 | 0 running, 1 timed out at 2:00, 2 finished |
| player final min/sec/tenths | c6 `+0xB9` / `+0xBA` / `+0xBB` | u8 each | what the result screen prints |
| trainer final min/sec/tenths | c6 `+0xB6` / `+0xB7` / `+0xB8` | u8 each | beat the trainer by editing his time |
| **player balloons** | c6 `+0xD3` | u8 | each balloon is -3 s on the tally |
| trainer balloons | c6 `+0xD4` | u8 | |
| **player birds** | c6 `+0xD5` | u8 | each bird is +3 s on the tally |
| trainer birds | c6 `+0xD6` | u8 | |
| **best times, persistent** | 0 | RVA `0x00D2CD24`, 4 x (u8 min, u8 sec, u8 tenths) | the saved record per course |

The clock and the 2:00 stop are verified from the clock actor's cascade. The balloon and
bird fields are verified as HUD values by their message-window field numbers. The
player-versus-trainer assignment rests on a window pairing, so call it strongly inferred.
The -3 and +3 semantics come from the shipped message text, and the tally is computed at
the result screen rather than applied to the live clock, which is why no code writes
plus or minus 3 to the clock vars.

**Correction to `MINIGAMES_TIMED.md` section 3.5**: it says nine bytes and three courses.
It is **twelve bytes and four courses**, class-0 `0xA8..0xB3`, RVA `0x00D2CD24` to
`0x00D2CD2F`. The fourth course was missed.

On `chocobo.swf`: **the score does not live in the Flash movie.** It is in the class-6
vars above, and `IggyChocoboSwfEnable` only gates loading a HUD overlay. The numbers reach
the screen through message-window fields driven from those vars, so the race is fully
playable with the swf absent.

#### Lightning dodge, `ka/kami0000`

| field | cls | address | width | why edit it |
|---|---|---|---|---|
| **dodge window open** | 6 | c6 `+0x7` | u8 | set to 45 by a Circle press and counted down. **Hold it non-zero and every bolt is dodged** |
| **current consecutive streak** | 6 | c6 `+0x12` | u16 | the live counter. Jump straight to 199 |
| bolts seen, persistent | 0 | RVA `0x00D2CE8C` | u16 | |
| bolts dodged, persistent | 0 | RVA `0x00D2CE8E` | u16 | |
| **best streak, persistent** | 0 | RVA `0x00D2CE90` | u16 | what the 5/10/20/50/100/150/200 reward tier switch reads |
| reward bits | 0 | RVA `0x00D2CE84` | u8 | `0x01..0x20` streak tiers, `0x40`/`0x80` the 30/80 total tiers |

#### Butterfly hunt, `mc/mcfr0100` - watch out, this one is class 3, which is per actor

| field | cls | address | width | why edit it |
|---|---|---|---|---|
| **countdown, seconds** | 3 | actor **37** base `+0x0` | s32 | init 39 for the 40 s course, 29 for the 30 s course |
| countdown, tenths | 3 | actor **37** base `+0x4` | s32 | init 9, with a 10/60 cascade |
| **gauge** | 3 | actor **38** base `+0x16` | u8 | starts at 128, blues `+= 3` at 14 sites, reds `-= 3` at 14 sites, a `>= 89` test gates the reward |

**The "red butterfly penalty" and the "caught count" are the same variable.** There is no
separate caught counter and no separate penalty counter, it is one gauge moved by plus or
minus 3. **What 128 and 89 actually mean could not be settled.** `mcfr0100` declares no
class-0 variables at all, so there is no save byte for "butterflies caught", and the
reward goes straight into the equipment table.

#### Jecht Shot practice, `sw/swin0000`

| field | cls | address | width | why edit it |
|---|---|---|---|---|
| **score** | 6 | c6 `+0x2` | u8 | |
| **attempt budget** | 6 | c6 `+0x1C` | s32 | the loop runs while this is below 300. Set it to 0 for unlimited attempts |

#### Via Purifico and the Cactuar hunt: no live state, and that is a measurement

`bvyt0500` declares 16 class-6, 2 class-3 and 1 class-0 descriptor in total. `bika0000`
declares 12, 11 and 12. **There is no clock** (the 10/60 cascade appears in no `bika` or
`bvyt` package), **no RNG** (the two rand syscalls have zero call sites in all seven of
them), no score and no gauge. Both are find-and-reach puzzles whose only state is a
handful of class-0 world flags, the same kind as a chest. The editor for them is the
generic class-0 browser.

#### The airship searches: probably nothing to edit

No coordinate-search or destination-search strings exist in the exe, and the destination
list goes through the same map warp every door uses, so these are destination-list
unlocks rather than timed minigames. **Treat as "unlikely to exist" rather than "verified
absent"**, since the `hiku*` packages were not opened.

#### Overdrive input minigames: already declared, do not redeclare

**These are all in `addresses/Battle.h` and `reversing/OVERDRIVES.md`. Point the plugin at
the existing constants.** That was checked rather than taken on trust. The whole live set
is native globals, not ATEL:

| already in `Battle.h` | RVA | what |
|---|---|---|
| `BtlOdMinigamePhase` | `0x00F3F77C` | 0 idle, 1 armed, 2 running. **the discriminator** |
| `BtlOdTimeBudget` / `BtlOdTimeRemaining` | `0x00F3F78C` / `0x00F3F790` | float seconds. **Raise the budget to widen every window** |
| `BtlOdTidusZoneLow` / `ZoneHigh` / `BarVel` | `0x00F3D73C` / `73E` / `744` | **widen the zone or zero the velocity to make it free** |
| `BtlOdAuronSeqLen` / `SeqIndex` | `0x00F3D700` / `6FC` | the required count and the current one |
| `BtlOdLuluGauge` / `GaugeMax` / `HitCount` | `0x00F3D71C` / `70E` / `71F` | max is 192, hits cap at 16. `HitCount` is the result quality |
| `BtlOdWakkaReelsActive` | `0x00F3C93F` | the reel logic itself lives in 10 magic DLLs, not the exe |
| `BtlActorOdResultOff` | actor `+0x0D28` | the result dword |

**One structural fact for the UI: the overdrive live state is a single global set, so two
characters cannot be inside an overdrive minigame at once.** `FFX_BtlMenu_DrawRoot
0x0049B360` has a ready-made "a minigame is on screen" predicate, the OR of the four state
bytes.

### One caution for the whole live-state editor

The class-0 editor writes into the save block, which is serialised byte for byte, so a bad
write there **follows the player into their save file**. The class-3 and class-6 editors
write into memory that is freed on the next map change, so a bad write there is transient
but will desync a lockstep peer immediately, because a desync hash over only the save
block cannot see a class-6 drift. For Blitzball specifically, prefer the cheat flag at
`0x008CCACC` over any direct poke.

---

## 7. THE ENUMERATION TABLE

One row per list the UI needs. This is the table the whole brief turns on, because a
picker with no list is half a feature.

| list | where it lives | stride and count | how to build a display label | runtime in process? |
|---|---|---|---|---|
| **Models** | `ChrRomIndexTables` RVA `0x00EFFAB8`, a `void *[7]` by category. Each entry is `{ s16 count; s16 number[count] }`, loaded from `chr/<dir>/<dir>.tbl` by `ChLoadRomIndexTables` from `FFX_Ch_Init`, no debug gate | 7 tables, `s16` entries. **892 total**: 35 pc, 348 mon, 239 npc, 31 sum, 80 wep, 117 obj, 42 skl | `sprintf("%c%03d", "cmnswfk"[cat], number)` gives `"c001"`. Add the dir name from `ChCategoryToDirName(cat)` for the group header. Merge a friendly name for the 8 characters and 10 aeons from the character list below | **YES, preferred.** No baked data at all. Guard on a null table pointer |
| **Characters** | `CharIndexToChrId` RVA `0x008423A0`, a `DWORD[18]` table in the exe | stride 4, **count 18** (8 characters then 10 aeons). Bounded by the unrelated dword at `+72` | `FFX_GetUnitDisplayName(idx)` RVA `0x004AC850` for all 18, localised. Cross-check the first 8 against `DebugCharNames 0x0083432C`, a `char *[8]` | **YES.** But **clamp to 0..17 yourself**, the engine's own guard is `<= 30` and index 18 returns the save-block CRC as a string |
| **Minigames and maps** | `eventid.bin` at `ffx_ps2/ffx/proj/event/header/eventid.bin`, 6741 bytes. In process it is `g_ffxEventIdTable` RVA `0x021D5888` with the count at `0x01534EC8`, built by `FFX_LoadEventIdTable 0x00507F50` | in memory 16-byte rows, `char name[12]` then `int id`. In the file `{u32 nameOffset, u32 nameLen}` pairs, `count = dword[0] / 8`. **402 rows**, 400 named | the name is a terse code like `nagi0000`. **The exe ships no human-readable minigame or area names**, so group by the 4-char prefix (66 distinct in the real data) and bake about 20 friendly names for the minigame rows | **YES for the list**, with two conditions. The count is 0 until something calls the loader, so the plugin calls it itself. And the **26 bad ids must be baked and filtered**, because `FFX_Ev_LoadEventPackage` hangs on `while(1)` for a missing package |
| **Monster Arena fights** | the `btl:002` sites in `nagi0700.ebp`, as packed battle ids | 36 fights | `(mapId << 16) \| encounterId`: `(601<<16)\|105`, `(603<<16)\|{76,77,79..83,92..99}`, `(604<<16)\|{0..18}`. No names in the exe | **NO, bake it.** The ids come from parsing bytecode, which a plugin should not do at runtime |
| **ATEL script variables** (the live minigame editor) | the per-package descriptor table at `*(u32 *)(actorDef + 0x14)` | 8 bytes per descriptor. `type = desc >> 28`, `class = (desc >> 25) & 7`, `offset = desc & 0xFFFFFF`, `count = *(u16 *)(desc + 4)` | var index plus the decoded type, and a baked label for the slots named in section 6 | **YES**, and this is what makes a generic typed variable browser possible. Re-resolve the base every frame, classes 2 to 6 are freed on every map change |

Two of the five rows need some baked data, and in both cases the reason is specific and
worth stating rather than hiding: the arena ids live in bytecode, and the bad-event-id
filter exists to avoid a hard hang. Everything else is pure runtime enumeration.

---

## 8. ADDRESS TABLE

RVAs. VA = RVA + `0x400000`. Added to `addresses/Character.h` or
`addresses/Minigames.h` by this pass unless the last column says otherwise.

### Models and the chrId encoding (added to `Character.h`)

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| `0x00EFFAB8` | data | `ChrRomIndexTables` | `void *[7]` | **THE model list.** `{s16 count; s16 num[]}` per category |
| `0x0042A5F0` | func | `ChLoadRomIndexTables` | `int (void)` | fills it from `FFX_Ch_Init`, no debug gate |
| `0x0042A640` | func | `ChFindRomEntry` | `int (int chrId)` | the load gate, -1 means cannot load |
| `0x00438100` | func | `ChIdToModelName` | `const char *(int chrId)` | `"%c%03d"` into ONE shared static buffer |
| `0x00F00A00` | data | `ChIdToModelNameBuf` | `char[16]` | that buffer. Format the name yourself instead |
| `0x00438150` | func | `ChIdToCategoryDir` | `const char *(int chrId)` | |
| `0x00429C90` | func | `ChCategoryToDirName` | `const char *(int cat)` | 0 -> `"pc"` |
| `0x00429E10` | func | `ChCategoryToLetter` | `char (int cat)` | 0 -> `'c'`, 7..15 -> `'-'` |
| `0x00429D80` | func | `ChLetterToCategory` | `int (int letter)` | `'c'` -> 0, else -1 |
| `0x00425A40` | func | `ChLoadChrData` | `CHRDATA *(int chrId)` | short-circuits on `ChFindChrData`, keyed on chrId |
| `0x00425EA0` | func | `ChFindChrData` | `CHRDATA *(int chrId)` | null on a miss |
| `0x00426070` | func | `ChBindChrData` | `int (Character *, CHRDATA *)` | writes `TidusChr` unguarded for `c001`/`c101` |
| `0x004277F0` | func | `ChBuildSkeletonInstance` | `void (Character *)` | per-allocation joints from this model's header |
| `0x0023D370` | func | `ChrAttachModelInstance` | `void (int *, ClassCharacter **, Character *, cb)` | fills `CHR+0x830` only. **Wrong tool for a swap** |
| `0x004295E0` | func | `ChDebugSpawnByName` | `void (int, char *name)` | the per-category motion setup reference |
| `0x0042B3A0` | func | `ChSetLocomotionMode` | `void (Character *, int)` | mode 1 for mon and sum |
| `0x00437D00` | func | `MotSetByModeIndex` | `(Character *, int mode, int idx)` | reads the model's own `.chr` section |
| `0x00439960` | func | `MotSetPendingLoopCount` | `void (Character *, int)` | |
| `0x0042B590` | func | `ChSetScaleUniform` | `void (Character *, float)` | Allocate passes `CHRDATA.m_defaultScale` |
| `0x0042AFE0` | func | `ChSetSlot` | `void (Character *, int slot, int value)` | below `0x1000` is a logical index |
| `0x0042DAD0` | func | `ChSetPlayerChr` | `void (Character *)` | move the binding before disposing the old body |
| `0x004266F0` | func | `ChDispose` | `void (Character *)` | nulls the player binding AND `TidusChr` |
| `0x004261F0` | func | `ChFindById` | `Character *(int chrId)` | linear pool scan for a live CHR |
| `0x00424F00` | func | `ChDataDispose` | `int (int chrId, int set)` | no-ops while any instance is alive |

CHRDATA offsets: `m_id` `+0x000`, `m_defaultScale` `+0x034`, `m_name` `+0x071`,
**`m_isFallback` `+0x0F8`**, size `0x12C`.

### Minigame launch and live state (added to `Minigames.h`)

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| `0x00D2CC7C` | data | `AtelScriptWork` | `u8[0x2000]` | **the LIVE class-0 area**, `SaveData+0x1EC` |
| `0x0046C2E0` | func | `AtelResolveVarAddress` | `char *(ctx *, u32 desc)` | the class 0..6 dispatcher |
| `0x0046C570` | func | `AtelResolveVarElemAddress` | `char *(ctx *, int varIdx, int elem)` | adds the bounds-checked index |
| `0x0046DFB0` | func | `AtelOpStoreVar` | opcodes `0x20/21/23/24` | proves the 8-byte descriptor stride |
| `0x00507F50` | func | `LoadEventIdTable` | `void (void)` | **callable in retail**, one-shot guarded |
| `0x021D5888` | data | `EventIdTable` | `struct { char name[12]; int id; } *` | 16-byte rows, `id == index` |
| `0x01534EC8` | data | `EventIdTableCount` | `int` | 402 once loaded, 0 before |
| `0x01534ECC` | data | `EventIdTableLoaded` | `int` | the one-shot flag |
| `0x00507E70` | func | `MapGetCurrentMapName` | `int (char *buf)` | `"????"` if the table is empty |
| `0x0046E990` | func | `AtelStartThread` | `int (int caller, int actor, int kind, int chan, int entry)` | no dedupe, `0xFFFF` = no caller |
| `0x004766D0` | func | `AtelFireActorEventWithEntry` | `int (int caller, int actor, int kind, int entry)` | keeps the engine's checks |
| `0x00381C90` | func | `BtlRequestScriptedBattle` | `int (int battleId, char, char)` | `battleId = (mapId << 16) \| encId` |
| `0x003827F0` | func | `BtlResolveBattleId` | `u8 *(int, int *, int *, int *)` | what proves the packing |
| `0x003C6D80` | func | `DebugBeginSelectedBattle` | `int (void)` | the shipped one-call battle launcher |
| `0x003AFD30` | func | `BtlOdStartMinigameForActor` | `int (int actorIndex)` | the overdrive minigame entry point |
| `0x003AFC90` | func | `BtlOdMaybeStartMinigame` | - | only passes ability kinds `0x20/0x25/0x30/0x40` |
| `0x0048D660` | func | `SaveDataGetMapId` | `u16 (void)` | `*(u16 *)GetSaveData()`, the LIVE map id |
| `0x00D2E0CE` | data | `BlitzHomeScore` | `u8` | class-0 `0x1452`. **verified** |
| `0x00D2E0CF` | data | `BlitzAwayScore` | `u8` | class-0 `0x1453`. **verified** |
| `0x00D2CD24` | data | `ChocoboBestTimes` | `u8[4][3]` | **4** courses, not 3 |
| `0x00D2CE8C` / `8E` / `90` | data | lightning seen / dodged / **best streak** | `u16` each | class-0 `0x210` / `0x212` / `0x214` |
| `0x00D2CE84` | data | `LightningRewardBits` | `u8` | class-0 `0x208` |
| `0x006431F0` | func | `DebugToggleFullNagi0700` | `BOOL (void)` | the Monster Arena fully unlocked |
| `0x00643210` | func | `DebugSetThunderPlainTreasureEnable` | `int (int)` | the 200-dodge package swap |
| `0x00390420` | func | `DebugSetOverdriveAlwaysFull` | `int (int)` | cheapest route to the overdrive minigames |
| `0x002B6790` | func | `DebugStepChocoboGameDebugEnable` | `int (char, char)` | needs 4 presses, just write the byte |

### Already declared elsewhere, pointed at rather than duplicated

| RVA | name | header |
|---|---|---|
| `0x0048EA60` | `MapRequestChange` | `addresses/WorldState.h` |
| `0x0046FEC0` | `MapWarpTo` (do not call, it is gated) | `addresses/WorldState.h` |
| `0x00508100` | `AutoTestJumpMap` | `addresses/WorldState.h` |
| `0x00F3C910` | `DebugMode` | `addresses/WorldState.h` |
| `0x00D2CA90` | `SaveData` | `addresses/GameState.h` |
| `0x008423A0` | `CharIndexToChrId` | `addresses/GameState.h` |
| `0x0083432C` | `DebugCharNames` | `addresses/GameState.h` |
| `0x00D32DDC` | `CharNames` | `addresses/GameState.h` |
| `0x00F26B28` | `AtelContextPtr` | `addresses/Atel.h` |
| `0x00F26B30` | `AtelSteppingContextIndex` | `addresses/Atel.h` |
| `0x0046F6A0` / `0x0046C1A0` | `AtelSetPlayerActorId` / `Get` | `addresses/Atel.h` |
| `0x0046EA00` | `AtelCreateThread` | `addresses/Atel.h` |
| `0x0049B360` | `BtlMenuDrawRoot` | `addresses/Battle.h` |
| `0x00F3F77C` and the whole overdrive set | `BtlOd*` | `addresses/Battle.h` |
| `0x0042B0D0` | `ChSetPartyIndex` | `addresses/Character.h` |

The party constants from section 4 belong in `addresses/GameState.h`, which this pass did
not own. They are listed at the end of section 4.

---

## 9. Corrections this pass makes to existing docs and headers

Recorded together because each one was a claim someone would otherwise have relied on.
All four were re-verified in the disassembly before being written down.

1. **`addresses/Minigames.h`: `AtelSysCore527ChocoboRace 0x0045C100` is not a chocobo
   race function.** Its 12 shipped call sites are all in `nagi0000`, which is why it got
   that name, but the body pops an int, sets the random-encounter scene override, and
   rebuilds the zone weights. It reads and writes no race variable. Renamed in the header
   to `AtelSysCore527SetEncounterSceneOverride`.
2. **`addresses/Minigames.h`: the `ScriptWorkAreaBackup` warning is wrong.** The header
   says "a restore can silently undo a replicated write". It cannot: both accessors are
   referenced only from `maybe_FFX_TkHarness_Init_DEAD`, which installs them behind a
   predicate returning a constant 0, so the pointers stay NULL. Exactly one xref each,
   both from that dead function. The header comment is corrected.
3. **`addresses/GameState.h`: `g_ffxCurrentMapId` at `SaveData+0xB8` is the checkpoint,
   not the live map.** High word map, low word entry point. The live map id is
   `SaveData+0x00`, which is what `FFX_SaveData_GetMapId 0x0048D660` returns. Not changed
   here because that header is not owned by this pass.
4. **`reversing/MINIGAMES_TIMED.md` section 3.5: the chocobo best times are 12 bytes and
   4 courses, not 9 bytes and 3 courses.** Class-0 `0xA8..0xB3`, RVA `0x00D2CD24` to
   `0x00D2CD2F`. Twelve separate class-0 `u8` descriptors.
5. **`tools/pcmodels.py`'s "safe to spawn" test is too strict**, and that matters because
   it is the obvious thing to build the model picker from. It tests `.chr` section 5
   (motion mode 0) and reports 349 of 859, but monsters and aeons use **mode 1**, so
   almost every monster is wrongly marked unusable. See the category branch in section 3.

---

## 10. What was not settled

Listed so nobody re-derives the same dead ends, and so the UI does not present a guess as
a fact.

- **Monster display names.** `battle/kernel/monster1.bin`, `monster2.bin`, `monster3.bin`
  and `name_txt.bin` hold the bestiary, but they are indexed by battle monster TYPE id
  and the models are indexed by `.chr` model number. The two spaces are related and the
  mapping was not established. **Do not fabricate a monster name from a model number.**
- **Whether the `s001` to `s011` aeon models carry a usable field motion bank.** That is
  a file question rather than an IDA one, and it decides whether a hand-rolled aeon field
  spawn animates or T-poses.
- **Whether firing a minigame's actor entry cold produces a playable minigame** or a
  half-initialised one that skips the party checks, item grants and camera the NPC
  dialogue normally does. The mechanism is proven, the outcome is not. The engine's
  reentrancy guards mean a wrong call is a quiet no-op or a stuck script rather than a
  crash, so this is cheap to test and should be the first runtime experiment.
- **Which ATEL context Blitzball loads into.** One of 0 to 5. A plugin that assumes
  context 0 and guesses wrong will silently write nothing, which is the worst failure
  mode on this list. Settle it with a breakpoint during a match and a read of RVA
  `0x00F26B30`.
- **The real-time rate of the Blitzball match clock.** The unit is verified as display
  seconds, the rate is not, because the tick loop has branch-dependent waits.
- **What the butterfly gauge's 128 start and 89 threshold mean**, and whether a separate
  caught count exists anywhere. Neither was found, so the single gauge is what is
  reported.
- **Every unnamed field in the 2560-byte Blitzball block.** The dimensions are solid, the
  meanings are guesses. Specifically class-0 `0x1528` and `0x1529` are read 24 and 2
  times by the match controller and written by no `bltz` package, so they come from a
  team-select screen. **Do not surface any of these in an editable UI.**
- **Whether warping straight to a minigame map leaves a co-op peer in a sane state.**
  `FFX_Scene_Init` stages the save file and requests map sync data on most transitions,
  and what those do to a joined peer was not traced.
- **The airship searches** beyond "no relevant strings in the exe and no `hiku*` package
  opened".
- **Whether `record[0x2C]` bit 2 (the party slot lock) is ever set at runtime.** It has
  four readers and no writer was found in the binary, so it may be purely save-data
  authored. Worth a runtime read before relying on clearing it.
