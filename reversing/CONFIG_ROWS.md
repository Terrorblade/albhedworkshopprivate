# FFX HD Remaster - appending rows to the native Config screen (menu module 10)

Target: `G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\FFX.exe`
32-bit, preferred base 0x400000. Addresses below are VAs at the preferred base unless
marked RVA.

Status: implemented and compiling. **None of it has run inside the game.** Everything in
section 11 is a prediction, not a result.

## What shipped, in one paragraph

Pilgrimage Together now builds its own array of Config row pointers in DLL memory, copies
the game's eight pointers into the front of it, appends five of its own, and points
`g_ffxMenuConfigRows` at it while a session is running. Labels come from **route B**: two
5-byte detours on the kernel string lookups, answering only for a reserved id range above
anything the real table holds, with the text encoded into FFX's glyph bytes at lookup time.
The encoding is fully solved, so the labels are real words and they are dynamic. A slot row
is labelled with the live character name and the help line at the bottom of the screen says
who is playing that character and whether they are connected. Nothing of the game's is
modified. No row object, no array, no string table.

## Scope

This doc is the native in-game Config screen, which is module 10 of the PS2-era module
system. It is not the PC settings overlay.

- `MENU_SYSTEM.md` - the module manager, the 25 descriptors, the slot meanings.
- `SETTINGS_MENU.md` - the Esc menu, which is Iggy/Flash and a completely separate thing.
- `MENU_SYNC.md` - how module 10 gets replicated at all, which is what makes any of this
  safe to drive from two machines.

## 1. The two globals

| VA | Name | What it is |
|----|------|-----|
| 0x186A44C | `g_ffxMenuConfigRows` | pointer to an array of row-object pointers |
| 0x186A448 | `g_ffxMenuConfigRowCount` | plain int, how many of them |
| 0x186A440 | `g_ffxMenuConfigCursorRow` | the highlighted row, read as a **signed char** |
| 0x186A438 | `g_ffxMenuConfigEditingRow` | permanently 0, see correction 5 |

Both of the first two are in the zero-filled tail of `.data`, so both are RW and both are 0
until the menu has been opened once. `FFX_Menu_ConfigSelectRowTable` (RVA 0x4CB350, VA
0x8CB350) writes them. That function is module 10's **PREPARE** slot, which means it runs
once per menu OPEN rather than once per entry to the Config screen. There is a lot of slack
between the player pressing Triangle and the player selecting Config.

The four candidate arrays, chosen on `FFX_GetLanguage() != 0` crossed with
`sub_A7AE90() == 1` (an HDD or asset check):

| VA | branch |
|----|--------|
| 0xC5A554 | lang != 0, hdd |
| 0xC5A578 | lang != 0, not hdd |
| 0xC5A598 | lang == 0, hdd |
| 0xC5A5C0 | lang == 0, not hdd |

Ten row objects sit at 0xC5A39C, stride 0x2C. Eight are used. 0xC5A4A4 and 0xC5A4FC are
built and never referenced by a live array.

## 2. One row, 44 bytes (0x2C), confirmed

Read off the decompile and then read back out of the image for all eight shipped rows, so
the "same in all of them" claims below are measured rather than eyeballed.

```
+0x00  int    0 in all ten rows
+0x04  int    VALUE COUNT. 2 in all eight shipped rows. Max 4, see +0x1C.
+0x08  int    CURRENT VALUE. Seeded by the getter on entry to the screen.
+0x0C  int    SELECTABLE. Up and Down skip any row whose value here is not 1.
+0x10  fn*    GETTER, __cdecl(row). Called once per row on entry. MUST NOT BE NULL.
+0x14  fn*    SETTER, __cdecl(row). Called on EVERY Left/Right press.
+0x18  WORD   the label's string id, kernel string group 7
+0x1A  WORD   3 in all ten rows, read by nothing
+0x1C  WORD   value 0's name id   \
+0x20  WORD   value 1's name id    |  stride 4, count = +0x04, so the array runs
+0x24  WORD   value 2's name id    |  +0x1C to +0x28 and four values is the ceiling
+0x28  WORD   value 3's name id   /   a 44-byte row can hold
```

The setter is "the value changed, apply it", not "apply on confirm". Module 10 has already
moved and wrapped `+0x08` before it calls you, so the new value is read back out of the row
rather than computed.

Four of the eight shipped getters write `+0x0C` themselves, so a dynamic selectable flag is
a thing the game already does.

### Drawing, which is what caps how much text fits

`FFX_Menu_ConfigDrawRow` (RVA 0x4CBAE0):

- Rows run from y = 220 with a pitch of **740 / rowCount**, in a 1080-tall UI space.
  Adding rows squeezes the existing ones rather than running off the bottom, and the glyphs
  do not shrink with the pitch.
- The label is drawn at x = 470, left aligned, text scale 0.78.
- The values are **centred** at x = 960 + 420\*i, measured with `FFX_Text_MeasureString` at
  0.68, greyed when `+0x0C` is 0. So the three columns centre at 960, 1380 and 1800 in a
  1920-wide space. The third column has 120 units to the right edge, which is about eight
  glyphs of value name.
- The help line is looked up for the cursor row's **currently selected value only**, with
  `FFX_Menu_GetUiStringDesc(thatValueId)` into `FFX_Menu_SetHelpString` (0x8AAEB0).

The 960 figure is worth spelling out because the code looks like 750. It adds
`FFX_UiScaleX(750)` and `FFX_UiScaleX(210)` separately and the scale is linear, so the
centre is at 960 unscaled.

`FFX_Menu_SetHelpString` keeps the **pointer** rather than copying the bytes, which is why
the string provider has two buffers per id and not one.

## 3. The eight shipped rows

Order as the live array holds them, with the statically initialised fields.

| idx | row object | label id | value ids | getter |
|-----|-----------|----------|-----------|--------|
| 0 | 0xC5A528 | 4 | 6, 5 | `FFX_Menu_ConfigRowGetSaveBit5EC` |
| 1 | 0xC5A39C | 10 | 11, 12 | `FFX_Menu_ConfigRowGetFlag_0x200` |
| 2 | 0xC5A3C8 | 13 | 14, 15 | `FFX_Menu_ConfigRowGetFlag_0x20` |
| 3 | 0xC5A3F4 | 4 | 6, 5 | `FFX_Menu_ConfigRowGetFlag_0x02` |
| 4 | 0xC5A420 | 16 | 17, 18 | `FFX_Menu_ConfigRowGetFlag_0x400` |
| 5 | 0xC5A44C | 22 | 23, 24 | `FFX_Menu_ConfigRowGetFlag_0x800` |
| 6 | 0xC5A478 | 19 | 20, 21 | `FFX_Menu_ConfigRowGetFlag_0x40` |
| 7 | 0xC5A4D0 | 25 | 26, 27 | `FFX_Menu_ConfigRowGetFlag_0x10_Vibration` |

Every one of them has value count 2, current 0, selectable 1, and the constant 3 at +0x1A.

Row 0 is the HD Remaster's music selector and its ids 4, 6 and 5 are **never read**, because
of the row-0 special case in the draw. Row 3 is the row that actually uses label id 4. The
highest id in use is 27, so a reserved base of 0x0F00 is a long way clear.

## 4. Corrections to the layout note this pass was handed

The note was right about the two globals, the 44-byte row, which function rewrites them, and
that both are RW `.data`. Nine things were wrong or incomplete, roughly in order of how much
trouble each one would have caused.

1. **`+0x1C` and `+0x20` are not two unknown ints.** They are the start of a value-name
   string id array, WORD each at **stride 4**, count = `+0x04`. The array runs `+0x1C` to
   `+0x28`. Writing them as ints would have given every row one correct value name and one
   wrong one, which is the kind of bug that looks like a text bug.
2. **`FFX_Menu_ConfigSelectRowTable` cannot take a 5-byte detour.** It has no prologue at
   all. Its first instruction is `E8 9B 0F FE FF`, a rel32 call to `j_FFX_GetLanguage`,
   which is exactly the five bytes a jmp wants. This is the third function in this binary to
   defeat a naive detour, and the brief was right to insist on checking.
3. **It is the PREPARE slot, so it runs once per menu OPEN**, not once per Config screen
   setup. That is the good news hiding behind correction 2. Polling the globals is cheap
   because the window is huge.
4. **`+0x18` as a dword is 0x00030004.** The low word is the label id as the note said. The
   high word is the constant 3 at `+0x1A` that nothing reads. Treating `+0x18` as a dword
   label id would have masked to 4 anyway and worked by accident.
5. **`g_ffxMenuConfigEditingRow` has no writer anywhere in the binary** and lives in the
   zero-filled tail of `.data`, so it is permanently 0. The "editing row" branch in the draw
   is therefore not a mode, it is a hardcoded special case for **row 0**, the HD Remaster's
   Original/Arranged music selector, whose text comes from the LocKit via
   `FFX_LocKit_GetLine(461, 464, 462)` rather than from kernel string group 7. This matters
   for route C: the "caller-supplied stack buffer" in that branch is a LocKit line, not a
   place arbitrary content can be put. Any row a mod appends has an index above 0 and takes
   the ordinary looked-up path.
6. **All four candidate arrays hold the same eight pointers in the same order** in this
   build, so the language and HDD branch is vestigial. The code still copies whichever one is
   live rather than hardcoding one, because that costs nothing.
7. **The arrays are not all eight pointers long in the image.** 0xC5A554 runs 9 before
   0xC5A578 starts, 0xC5A598 runs 10. The trailing entries point at the two unused pool
   rows. Nothing reads them because the count decides, but do not take an array's length in
   the image as the row count.
8. **`g_ffxMenuConfigCursorRow` is read as a signed char and nothing clamps it** against a
   count that shrank. Up decrements and then wraps to `count - 1`, so with a count of 0 that
   is -1 and the exec dereferences `rows[-1]`. Shortening the table under a live cursor is
   the one way this feature could crash the game, and both the restore path and
   `SetConfigRowTable` guard it.
9. **Up and Down loop forever if no row is selectable.** Not reachable with the shipped eight
   because they are all selectable, but it means a mod must never leave every row it owns
   unselectable while also being the whole table.

Also worth knowing: the note said the shipped rows use ids 4 to 0x25. Observed maximum is
27.

## 5. The label question: route B shipped

### 5.1 The glyph encoding, solved

`byte = 0x30 + glyph index`, with 94 printable glyphs. The table is ASCII order with the
digits lifted to the front and `@` dropped:

```
index  0..9   0 1 2 3 4 5 6 7 8 9
index 10..25  space ! " # $ % & ' ( ) * + , - . /
index 26..31  : ; < = > ?
index 32..57  A..Z
index 58..63  [ \ ] ^ _ `
index 64..89  a..z
index 90..93  { | } ~
```

So space is 0x3A, `A` is 0x50, `a` is 0x70, `0` is 0x30. Bytes below 0x30 are control codes
and most of them eat a following byte.

Derived from `FFX_Text_DecodeGlyph` (0x8B7240) and `FFX_Text_ClassifyByte`, then
**round-tripped byte for byte against three strings the shipped game itself feeds to the
renderer**: LocKit lines 461 "Music", 462 "Original" and 464 "Arranged", pulled out of the
game's VBF with the project's own `tools/vbf.py`. That is the part that turns this from a
plausible table into a checked one. Those three are the music row's own text, which is the
one place the game puts a stack buffer of encoded glyphs through the same draw call a
looked-up string takes.

`EncodeFfxText` and `FfxTextEncodedLength` in the kit do the conversion. Anything outside the
94 glyphs is dropped rather than substituted, so a stray character shortens the label instead
of drawing a wrong glyph.

### 5.2 Where to hook, and the function that cannot be hooked

`FFX_Menu_GetUiString` (0x8DC9B0) is the obvious place and it is **not hookable** with a
5-byte detour, because its fourth byte starts a rel32 call. The two functions underneath it
are:

| VA | Name | Prologue |
|----|------|----------|
| 0x78FCF0 | `FFX_KernelString_Get(group, id, lang)` | `55 8B EC 8B 45 08` |
| 0x78FBB0 | `FFX_KernelString_GetDescription(group, id, lang)` | `55 8B EC 8B 45 08` |

Six whole position-independent bytes each. Both are detoured, **all or nothing**: if either
is refused the provider is cleared and the install fails, because answering the label but not
the description would leave the help line looking up our out-of-range id in the real table.

Ids are masked to 12 bits by the lookup, so the usable space is 0..4095 and the reserved
range is 0x0F00 plus 20.

### 5.3 Why route B and not A or C

Route A, reusing group 7 ids, was the shipping fallback and would have given five rows
labelled with whatever the menu already says. For a feature whose entire job is telling the
player who is playing Wakka, wrong labels are close to useless.

Route C, hooking the draw, means reimplementing the measure-and-centre maths and losing the
help line, and correction 5 shows the branch that looked like a free text path is a LocKit
special case for row 0.

Route B turned out to be workable because the encoding was checkable against the game's own
bytes rather than guessed. The payoff beyond "correct words" is that labels are produced at
lookup time, so they are **live**. A slot row's label is the current character's name, which
changes with the in-battle Switch command, and the help line can say "Player 2 is not
connected" without the row text having to fit in 120 units of screen.

## 6. The rows this mod adds

Five rows, appended behind the game's eight, only while a session is active.

| row | label | values | selectable |
|-----|-------|--------|-----------|
| 0 | `Co-op` | none | no, it is a heading |
| 1 | live name of active party slot 0, or `-` | `Host` / `Player 2` / `Player 3` | only if the slot has a character |
| 2 | slot 1 | same | same |
| 3 | slot 2 | same | same |
| 4 | `Menu control` | `Normal` / `Host only` | yes, on clients too |

No input delay row and no lockstep enforce row, as instructed.

Two text decisions that are not cosmetic:

- **Nothing is appended to a value name.** The third value column centres at 1800 of 1920,
  so "Player 3 (away)" would run off the right edge, and only on the third column, which a
  two-player test would never hit. Whether a peer is actually connected goes in the help
  line, which has room. Eight glyphs is the budget and it is the same as the game's own
  longest value name.
- **The override row is "Menu control" and not "Host drives menus".** Seventeen glyphs at
  the 0.78 label scale reaches about x = 868, which is where the first value column's left
  edge sits.

The value **count** on a slot row is `MaxPlayers` whoever is connected. A count that followed
the peer set would have the two machines cycling through different lists off the same
replicated press.

The override row is **selectable on a client** even though only the host may change it. Up
and Down loop until they land on a selectable row and the navigation runs off replicated
input, so two machines with different selectable flags put their cursors on different rows,
and the next Left or Right then changes two different settings. A refused press costs a log
line. A diverged cursor costs the session. Same reasoning puts the slot rows' selectable flag
on "is there a character in this slot", which is replicated state, rather than on anything
local.

## 7. How a press becomes an ordered command

The setter never writes state. Not ownership, not the override flag, nothing.

```
Left/Right on a co-op row
  -> module 10 moves and wraps row[+0x08] itself
  -> CoopRowSetter(row)
       reads the new value back out of the row
       sets the per-row ask latch (value + the step it was asked on)
       returns immediately unless MenuDriver() == LocalPeerIndex()
  -> slot row:      Lockstep::RequestCommand(kCommandCharOwner, {peer, charIndex})
     override row:  RequestMenuControlOverride(), the same function ctrl+F4 calls
  -> host stamps it for a step, both machines receive it
  -> the per-step tick drains CommandsForStep and calls SetCharacterOwner on both
  -> the next RefreshRow sees state == asked-for and drops the latch
```

`kCommandCharOwner = 6` in `net/Commands.h`, payload `{uint8_t peer, uint8_t charIndex,
uint8_t reserved[2]}`, with 0xFF meaning unbind.

**Why the setter is gated on the menu driver.** It runs on every machine, because every
machine stepped module 10 from the same replicated pad block. Without the gate one press
produces one identical command per player. `MenuDriver()` is the same answer on both
machines, which is what makes gating on it safe rather than another source of divergence.

**Why the latch exists.** The row holds the pressed value until the command lands, otherwise
it snaps back the instant the player presses Right and jumps again when the command arrives,
which reads as the press being lost. The latch clears when the authoritative state agrees
with what was asked for, which is the only definition of done that does not guess.

**Why the latch has a timeout.** 60 simulation steps, two seconds at 30 Hz. An ask can reach
nobody: a client pressing the host-only override row, a refused command, no menu driver
because MenuSync is not running. Without the timeout the row would sit on the player's chosen
value for the rest of the session while the state underneath said something else. It is
counted in **steps** rather than frames so the two machines give up on the same step and
their rows never disagree.

**The hotkey and the row are one path.** ctrl+F4 drains to `RequestMenuControlOverride()` and
so does the row. No change to `hooks/Hotkeys.cpp` was needed, and `hooks/FrameHook.cpp` only
gained a line in the ctrl+F11 dump.

## 8. Re-applying the array without a hook on the prepare slot

Correction 2 rules out a detour on `FFX_Menu_ConfigSelectRowTable`, and correction 3 says one
is not needed. The per-step tick polls `g_ffxMenuConfigRows` and re-injects whenever it has
gone back to a shipped array.

The detection is **pointer identity against our own array**, not "is this one of the four
shipped arrays". Anything else in there belongs to another mod, and claiming it would have
the string provider answering for rows it did not build. That case gets one log line and then
leaves the screen alone.

`SetConfigRowTable` writes in an order that is safe against a reader between the two stores:
shrinking writes the count first, growing writes the pointer first, and either way it clamps
the cursor afterwards. Correction 8 is why.

The restore defers while the Config screen is up, and in the meantime marks our rows
unselectable so a press during the wait cannot ask for anything. If nothing was ever saved it
logs and leaves the table alone, because setting it to NULL with a count of 0 is the
`rows[-1]` crash in correction 8.

Solo play never gets here. Nothing is injected until a session is active, so a solo player's
Config screen is the shipped game's.

## 9. The per-simulation-step tick, and a detour I would rather not have

An ordered command may only be consumed on the step it was stamped for.
`Lockstep::CommandsForStep` matches the current step exactly and `AdvanceStep` retires
anything at or behind it. So a consumer on the frame path silently misses a command whenever
one presented frame covers two simulation steps, which is what a catch-up step is. For an
ownership binding, missing it on one machine is exactly the unrecoverable split brain.

`FFX_MainStep` is the natural home and the kit's lockstep layer already owns it, with one
gate and one observer and no spare slot. `net/LockstepLink.cpp` was on the do-not-touch list
for this pass. So the tick is a 5-byte detour on:

```
FFX_MainStep_SetTopOfStepFloat   RVA 0x31E460   VA 0x71E460
  55           push ebp
  8B EC        mov  ebp, esp
  83 7D 08 00  cmp  [ebp+arg_0], 0
```

Seven bytes, three whole instructions, nothing position dependent, plain `C3` return so
`__cdecl`. `FFX_MainStep` calls it at 0x820B04 with a constant 0, unconditionally, in the
straight-line run before its first conditional branch, and it has exactly one caller in the
binary. The function writes one float and does nothing else.

**The right fix is for the lockstep gate to call a step function next to `PrepareMenuInput`
and for this detour to go away.** It is written down in the code comment too.

## 10. Addresses parked outside `addresses/`

The brief reserved `workshop/include/ffx/addresses/` to the user, so everything new is in a
`ParkedRva` namespace at the top of `workshop/src/ffx/MenuSystem.cpp` with a loud comment.
All RVAs. These want moving into an `addresses/` file.

| RVA | Name | Used by |
|-----|------|---------|
| 0x4CB350 | `MenuConfigSelectRowTable` | documented, not called |
| 0x4CB320 | `MenuConfigDrawScreen` | `ConfigScreenActive` |
| 0x4CB3C0 | `MenuConfigInitRowValues` | documented |
| 0x4CBAA0 | `MenuConfigRowAt` | documented |
| 0x4CBAB0 | `MenuConfigDrawAllRows` | documented |
| 0x4CBAE0 | `MenuConfigDrawRow` | documented |
| 0x85A554 / 0x85A578 / 0x85A598 / 0x85A5C0 | the four row arrays | `ConfigRowTableIsShipped` |
| 0x85A39C | `MenuConfigRowPool`, 10 rows | `ConfigRowTableIsShipped` |
| 0x146A438 | `MenuConfigEditingRow` | documented |
| 0x4DC9B0 | `MenuGetUiString` | documented, cannot be hooked |
| 0x4DC970 | `MenuGetUiStringDesc` | documented |
| 0x38FCF0 | `KernelStringGet` | **detoured** |
| 0x38FBB0 | `KernelStringGetDesc` | **detoured** |
| 0x4AAEB0 | `MenuSetHelpString` | documented |
| 0x505AB0 | `TextDrawString` | documented |
| 0x505290 | `TextMeasureString` | documented |
| 0x31E460 | step tick, in `plugins/pilgrimage/menu/CoopConfig.cpp` | **detoured** |

The two globals in section 1 were already wired.

## 11. Untested, ranked by how likely it is to be wrong

Nothing here has run in the game. In rough order of risk.

1. **Thirteen rows at 740/13 = 57 units of pitch may overlap.** The glyphs do not shrink
   with the pitch and the game's own eight sit at 92.5. The label baseline is 8 units into
   each row and the separator line is at 3. If 0.78-scale text is taller than about 50 units
   in the 1080-tall space, rows touch. This is the single most likely thing to look wrong on
   first run, it is cosmetic, and the cheapest fix is dropping the header row.
2. **The reserved id range may collide with something the menu asks for.** 0x0F00 is clear of
   everything the Config screen uses (max 27) but the provider is consulted for **every**
   group 7 lookup in the whole menu, not just the Config screen. If anything else in the menu
   legitimately asks for an id in 0x0F00..0x0F13 it gets our text. The provider checks the
   group and the range and that our rows are actually injected, which narrows it, but I have
   not enumerated group 7's real id space.
3. **`FFX_Menu_SetHelpString` keeping the pointer.** The two-buffers-per-id design assumes
   the help line reads our buffer later in the same frame, from a pointer it stored. If it
   holds that pointer across frames and our buffer is rewritten by a different id's lookup
   first, the help line shows the wrong text. It would never crash, because the buffers are
   static and NUL terminated.
4. **The step tick's call site.** The detour target is called once per `FFX_MainStep` from
   exactly one place with a constant argument, which is as good as this gets without editing
   the lockstep layer, but "once per simulation step on every path" is read off the one
   caller rather than observed.
5. **What `ConfigScreenActive` actually answers.** It is `ModuleStepping(10)`, which is the
   same test `FFX_Module_StepAll` makes: the descriptor exists, the module is not in the
   suspended mask, it has an exec, and its active byte or that byte's shadow is set. The
   dangerous direction is it reading false while the screen is up, because then the restore
   shortens the table under a live cursor, and that contradicts the engine's own step test.
   The likely direction is it reading true more broadly than "the player is looking at the
   Config screen", since module 10 is prepared at menu open, and all that costs is a restore
   that waits until the whole menu closes.
6. **The encoding's control codes.** Only the 94 printable glyphs are produced. If the
   renderer wants a leading control byte on a string to set colour or width, our labels may
   draw in a default that looks different from the game's. All three verification strings
   round-tripped without one, so this would be a subtlety rather than a break.
7. **The ask latch's two-second timeout** is a guess at a generous round trip. Too short and
   a slow link makes the row flick back before the command lands. It is only cosmetic and it
   is identical on both machines.
8. **`MenuDriver()` returning -1 mid-press.** If no driver is reported on the step the setter
   runs, nobody sends the ask, the latch holds, and the timeout puts the row back. The press
   is lost silently apart from the log line. Correct, but a player would call it a bug.
9. **The 44-byte row objects in DLL memory.** They are four-aligned as eleven DWORDs and the
   game only reads ints and WORDs out of them, so alignment is not a real worry. Listed
   because it is an assumption rather than a check.

Not a risk, stated for completeness: the game's eight row objects and its four arrays are
never written. Our array is in DLL memory and only the two globals are poked.

## 12. Files

Mine:

- `loader/plugins/pilgrimage/menu/CoopConfig.h`
- `loader/plugins/pilgrimage/menu/CoopConfig.cpp`

Kit:

- `loader/workshop/include/ffx/MenuSystem.h` - the Config row accessors, the encoder, the
  string override
- `loader/workshop/src/ffx/MenuSystem.cpp` - and the parked RVAs

Wiring, minimal:

- `loader/plugins/pilgrimage/net/Commands.h` - `kCommandCharOwner` and its payload
- `loader/plugins/pilgrimage/net/NetLink.cpp` - start and stop with the session
- `loader/plugins/pilgrimage/PilgrimageMod.cpp` - non-fatal install
- `loader/plugins/pilgrimage/ui/ControlPanel.cpp` - a readout line
- `loader/plugins/pilgrimage/hooks/FrameHook.cpp` - a line in the ctrl+F11 dump
