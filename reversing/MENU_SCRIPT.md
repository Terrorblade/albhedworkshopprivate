# The menu layout scripts: menu_script.bin, system_script.bin, battle_script.bin

Written 2026-10-28 for Project Zanarkand item 1.12.7. Every address is an IDA VA in FFX.exe (image
base 0x400000). `[V]` was read in Hex-Rays or the disassembly this session, `[I]` is inferred and
says from what. Function names are the ones `tools/apply_menuscript_names.py` proposes (section 10),
with the current `sub_` name beside them where it matters for finding them in an unpatched IDB.

The tool is `tools/menuscript.py`. It reads the install at run time through `tools/vbf.py` and writes
nothing but the picture `render` is asked for.

```
python -I tools/menuscript.py survey                 parse all six copies to the last byte
python -I tools/menuscript.py list [--file system]   every value with its index and entry offset
python -I tools/menuscript.py scenes [NAME]          scenes, tracks and decoded sequences
python -I tools/menuscript.py screens                which names each menu loader reads
python -I tools/menuscript.py render @config -o out.svg
```

## 1. Summary

- **One format, three files.** `menu/menu_script.bin` (18,736 bytes), `menu/system_script.bin`
  (1,008) and `menu/battle_script.bin` (35,584) are read by the same small library at
  0xA7B220..0xA7C8C0. Each file holds two things: **values**, named constants the menu code asks for
  by string (`rect_`, `pos_`, `col_`, `num_`), and **scenes**, keyframed animations of 2D primitives
  that code starts by name and that play inside the draw call. [V]
- **The "489 layout names" are 412 values and 35 scene names**, 447 names in all. 489 came from a
  printable string scan, which also picks up colour words such as `xxxF` (0x46787878). system_script
  has 16 values and 2 scenes (the old note said 22 names), battle_script 191 values and 34 scenes
  (the old note said 211). [V, survey]
- **The node graph is a hash table plus an animation graph**, not a widget tree. There are no parent
  and child links. Values sit in 16 hash buckets. A scene points at a track list, a track at a
  sequence, a sequence is a stream of ops. Draw groups list scenes per draw priority. [V]
- **A screen is not stored in the file.** A menu module's loader copies the values it needs into its
  own globals by name, and its draw function places plates from those globals plus constants in the
  code. The file supplies the geometry and colours, the code supplies the composition. [V]
- **system_script.bin is loaded and never read.** TOInit registers it as slot 0 and no call in the
  binary passes slot 0. All 16 of its values also exist in menu_script.bin, with one y differing. [V]
- **The main menu (module 1) does not use menu_script geometry at all.** Its draw function
  `sub_8E0BA0` lays out with HD constants through `FFX_UiScaleX/Y`, reads no value from the file and
  overwrites two of them every frame. Only the menu background colours `col_menubg0/1` reach it,
  through `FFX_Module_DrawAll`. This matters for 6.4.2. [V]
- **menu_script.bin is presentation only. battle_script.bin is not**: the overdrive minigame state
  machines wait for battle_script scenes to finish, and those scenes advance once per draw call.
  Section 8. [V for the call sites, the battle consequence is outside this item]
  (2026-10-28, MENU_DRAW_PATH.md 12: that draw call is once per non-catch-up step and never on a
  catch-up step, so the scene timing depends on the catch-up pattern.)

## 2. Where the files come from

`TOInit 0x8AD4D0` (from `tklib__f88D710`, from `FFX_MainInit`) loads all three into fixed buffers
and registers them as slots of `g_ffxMenuScriptSlots 0x22FB4DC` (8 dwords, `dword_22FB4DC`): [V]

| slot | file | path table | load site | buffer |
|---|---|---|---|---|
| 0 | `system_script.bin` | kind 18 entry 20, always `jppc` | `0x8AD69E` | `g_ffxSystemScriptBuf 0x185F610`, 0x800 bytes |
| 1 | `menu_script.bin` | kind 18 entry 21, always `jppc` | `0x8AD6AC` | `g_ffxMenuScriptBuf 0x185FE10` |
| 2 | `battle_script.bin` | kind 18, or kind 4 (`uspc`) when `FFX_GetLanguage` is 9 or 17, entry 22 | `0x8AD682` | `g_ffxBattleScriptBuf 0x1841E10` |

```
0x8AD690  push offset unk_185F610 / push 14h / push 12h / call sub_8ABA30   ; kind 18 entry 20
0x8AD6A3  push offset unk_185FE10 / push 15h / push 12h / call sub_8ABA30   ; kind 18 entry 21
0x8AD6B1  call sub_8ADF20                                                   ; FFX_MenuScene_ClearAllObjects
0x8AD6B6  call __cfltcvt_init_150                                           ; FFX_MenuScript_ClearSlots
0x8AD6BB  push offset unk_185F610 / push 0 / call sub_A7C7F0                ; SetSlot(0, system)
0x8AD6C7  push offset unk_185FE10 / push 1 / call sub_A7C7F0                ; SetSlot(1, menu)
0x8AD6D3  push offset unk_1841E10 / push 2 / call sub_A7C7F0                ; SetSlot(2, battle)
0x8AD6E2  call sub_8AE6F0                                                   ; FFX_MenuLayout_LoadCommon
```

The entry numbers were matched against the path table with `projectzanarkand/tools/asset_paths.py`'s
reader: kind 18 entries 20, 21, 22 are `host0:/ffx/master/jppc/menu/{system,menu,battle}_script.bin`.
`sub_8ADEE0` (from `FFX_Map_ReturnToTitle`) clears the scene objects and slots and registers the same
three buffers again without reloading. `TOSwapInternationalEnvExec` and its Sync twin reload slot 2
only (`INTERNATIONAL_CONTENT.md` 4). [V]

So every language reads the **jppc** `menu_script.bin`. The jppc and uspc copies differ in 9 values,
all name entry and config row geometry (`pos_kanaa`, `pos_kanaga`, `pos_kanama`, `pos_ace`,
`rect_nmnplt`, `r_cnameplt`, `r_cnameplt_nonhdd`, `num_cnameplt`, `num_cnameplt_nonhdd`). The uspc
copy is never loaded. system_script is byte identical in both trees. battle_script differs in 11
values (`dum28..30`, `dum37..39`, `dumm1..3`, `dumm37`, `dumm38`). [V, survey]

The files are used **in place**. There is no parse step: the lookups read the buffer directly, and
scene playback writes its run time state (track counters, scene flags, scene offsets) back into the
buffer. The loaded file is live state, not a constant. [V, `FFX_MenuScript_ResetScene 0xA7BA70`,
`FFX_MenuScript_StepTrack 0xA7B510`, `FFX_MenuScript_SetSceneOffset 0xA7C810`]

**Thread.** Loading is at boot from `FFX_MainInit`. Lookups run in menu module loaders and in
`FFX_MenuLayout_LoadCommon`. Scene objects are stepped by `FFX_Module_StepAll` and drawn by
`FFX_MenuObj_DrawAllAtPrio` from `FFX_Module_DrawAll`, both inside `FFX_MenuSys_StepFrame`, which runs
under `FFX_MainStep` (`MENU_SYSTEM.md` 1). So it is the step thread, and the 2D list it fills is
consumed by the present side (`TEXT_RENDERING.md` 9). [V for the call chain, I that `FFX_MainStep`
is the step thread, from `MAIN_LOOP.md`]

## 3. The format

Little endian. Every offset is from the start of the file.

### 3.1 Header, 0xD0 bytes

| offset | type | meaning | read by |
|---|---|---|---|
| 0x00 | u32[16] | value buckets, 0 = empty | `FFX_MenuScript_FindValue 0xA7B7D0`: `mov esi, [edx+eax*4]` at 0xA7B81A after `and eax, 0Fh` at 0xA7B816 |
| 0x40 | u32[16] | scene buckets | `FFX_MenuScript_FindScene 0xA7B920`: `+ 4 * hash + 64` |
| 0x80 | u32 | sequence table offset | `FFX_MenuScript_RunScene 0xA7B460`: `mov eax, [ecx+80h]` (also at 0xA7B3FB) |
| 0x84 | u32[3] | always 0 | nothing in the library reads them |
| 0x90 | u32[16] | draw groups, 0 = empty | `FFX_MenuScript_DrawGroupScene 0xA7B220`: `mov eax, [edx+eax*4+90h]` at 0xA7B246, group clamped to 0..15 |

The bucket of a name is the xor of its bytes, `& 15`. The loop is 0xA7B7FB..0xA7B80D. The survey
checks every name sits in its own bucket. [V]

### 3.2 Values

A value bucket is `u32 count` then `count` entries. An entry is:

```
u32  header      low 24 bits: offset of the NUL terminated name. high 8 bits: n, the value count
u32  value[n]
```

`FindValue` compares the name at `header & 0xFFFFFF` (`and eax, offset unk_FFFFFF` at 0xA7B837,
IDA shows the mask as an address) with `strcmp`, and on a miss steps `1 + (header >> 24)` dwords.
It returns the **entry offset** from the file start, or 0. That offset is the game's handle for a
value, so `menuscript.py list` prints it. `FFX_MenuScript_GetValueWord 0xA7B8C0(slot, entry, i)`
returns the u32 at `entry + 4 + 4 * i` with no bound on `i`. [V]

In the shipped files n is 1, 2 or 4:

| n | menu | system | battle | read as | getter |
|---|---|---|---|---|---|
| 1 | 146 | 9 | 138 | a colour (bytes R, G, B, A, PS2 alpha 0x80 = opaque) or a word, or `(s16)` for `num_` | `FFX_MenuLayout_GetWord 0x8AE360`, `GetBytes 0x8AE3A0`, `GetShort 0x8AE410` |
| 2 | 158 | 3 | 40 | a point x, y | `FFX_MenuLayout_GetPoint 0x8AE450` |
| 4 | 108 | 4 | 13 | a rect x, y, w, h | `FFX_MenuLayout_GetRect 0x8AE4B0` |

Points and rects are stored as u32 and read back through a `_WORD`, so they are s16. 13 menu values
hold a negative number (stored sign extended), none needs more than 16 bits. The units are the PS2
frame, 512 x 416 (section 5). All 108 menu rects but 3 (`r_wmojiplt1`, `r_rabsnmplt`, `r_risnmplt`)
fit inside it. [V, survey]

The prefixes are a naming habit, not a type tag. The getter decides how a value is read. `rect_` and
`r_` are rects, `pos_` and `p_` points, `col_` and `c_` colours, `num_` counts and spacings. battle
values have no prefix (`kuro` 0x80000000 is black, `siro` 0x80FFFFFF white, `dum0..`, `dumm..`). Names
are plain ASCII identifiers, not FFX's glyph encoding: the lookup `strcmp`s them against C string
literals in FFX.exe. [V]

### 3.3 Scenes

A scene bucket is `u32 count` then `count` entries of 12 bytes:

```
u32  name           offset of the name
u32  tracks         offset of the track list. Bit 31 = running, bit 30 = not finished. 0 in the file
s16  x, y           scene offset, 0 in the file, set at run time by FFX_MenuScript_SetSceneOffset
```

A track list is `u32 count` then `count` tracks of 20 bytes. Only the first dword is data, the rest
is run time state that the file holds in its initial form (the survey checks it):

| offset | type | meaning |
|---|---|---|
| +0 | u32 | sequence index, bit 31 = this track is alive |
| +4 | u16 | frame counter |
| +6 | s16 | keyframe duration, -1 = not tweening |
| +8 | u16 | pc, in u16 words from the sequence start |
| +10 | u16 | previous pc, the keyframe being drawn |
| +12 | s16 | the track being waited on, -1 = none (file: 0xFFFF) |
| +14 | u16 | loop start pc |
| +16 | u16 | loop counter |
| +18 | u16 | unused, 0 |

The sequence table (header +0x80) is a u32 offset per sequence. Its length is not stored. It ends
where the first track list starts, and the sequences tile the bytes before it exactly, each ending
with op 0. [V for the layout as the survey checks it, the library reads only `table[index]`]

### 3.4 Sequences and ops

A sequence is a stream of u16 words. `op = word & 0x7FF`, the length in words is
`g_ffxMenuScriptOpWords 0xC89C40` (`2 2 2 2 10 10 12 12 2 2 16`). The top five bits are flags. Read
off `FFX_MenuScript_StepTrack 0xA7B510` (jump table at 0xA7B770) and
`FFX_MenuScript_DrawKeyframe 0xA7BBC0`: [V]

| op | words | name | effect |
|---|---|---|---|
| 0 | 2 | end | clears the alive bit, the track keeps drawing its last keyframe |
| 1 | 2 | blank | marks itself as the last keyframe, so the track draws nothing from here |
| 2 | 2 | wait | waits while another track is alive. **Bug:** the search compares the argument with track 0's sequence on every pass (`mov ecx, [eax+4]` at 0xA7B5B7 is loaded once before the loop at 0xA7B5C6), so only track 0 can be waited on, and any other argument falls straight through |
| 3 | 2 | hide | draws nothing for `arg` frames |
| 4 | 10 | rect | keyframe: `+2` frames, `+4` colour0, `+8` colour1, `+12` x, `+14` y, `+16` w, `+18` h |
| 5 | 10 | line | keyframe: x, y to `+16`, `+18`. Its draw is `nullsub_159 0x8FE070`, **lines are not drawn on PC** |
| 6 | 12 | shape | keyframe: x, y, `+16` x scale %, `+18` y scale %, `+20` shape id |
| 7 | 12 | digits | keyframe, draws "123456789" at x, y. Unused |
| 8 | 2 | loop | jump back to the loop start `arg` more times. Flag 0x800 = forever |
| 9 | 2 | loop start | records the pc after itself |
| 10 | 16 | op10 | keyframe, `sub_8928B0(+28, x, y)`. Unused |

Flags on a keyframe word: `& 0x7000` picks the easing of the tween that starts at it
(`FFX_MenuScript_Ease 0xA7C6F0`: 0x2000 ease out `1-(1-t)^2`, 0x3000 ease in `t^2`, 0x4000 cosine,
else linear), and 0x8000 adds the scene offset to x, y (and to the line end for op 5).

A keyframe with duration d tweens from itself toward the next op over d draw calls when the next op
is the same primitive, field by field, `value = (1 - t) * next + t * current` with `t` going from
`(d-1)/d` to 0, colours per byte. When the next op is a different primitive it snaps at `t = 0.5`. A
duration of 0 holds the keyframe and moves on in the same call. [V, `FFX_MenuScript_DrawTrack 0xA7B290`]

Op use in the shipped files [V, survey]:

| file | rect | line | shape | blank | wait | hide | loop | sequences |
|---|---|---|---|---|---|---|---|---|
| menu | 136 | 0 | 14 | 3 | 3 | 1 | 3 | 79 |
| system | 7 | 0 | 0 | 0 | 0 | 1 | 1 | 5 |
| battle | 88 | 347 | 314 | 40 | 165 | 2 | 59 | 416 |

menu sequence 77 is `hide 1, loop forever, end`. It is the last track of most menu scenes, and its
only job is to keep the scene alive so that it never reports finished. [V, `menuscript.py scenes`]

### 3.5 Draw groups

Header +0x90 holds 16 lists, `u32 count` then scene entry offsets. A scene is run only when the
group being drawn lists it. Every shipped scene is in exactly one group: menu groups 0, 3, 4 and 8,
battle 0, 1 and 4, system 0 and 3. [V, survey]

### 3.6 Padding

What the structures do not cover is zero: up to 3 bytes to a 4 byte boundary inside the file, and
the tail to a 16 byte boundary. The survey fails on anything else. [V]

## 4. The menu side

### 4.1 Value getters

All take `(slot, name, out...)` and write zeros when the name is missing. [V]

| function | does |
|---|---|
| `FFX_MenuLayout_GetWord 0x8AE360` | u32 |
| `FFX_MenuLayout_GetBytes 0x8AE3A0` | u32 split into 4 bytes |
| `FFX_MenuLayout_GetShort 0x8AE410` | low 16 bits |
| `FFX_MenuLayout_GetPoint 0x8AE450` | two u16 |
| `FFX_MenuLayout_GetRect 0x8AE4B0` | four u16 |
| `FFX_MenuLayout_MenuWord 0x8C2AD0`, `MenuShort 0x8C2B10`, `MenuPoint 0x8C2B50`, `MenuRect 0x8C2B90` | the same with slot 1 |
| `FFX_MenuLayout_LoadPanel 0x8C1A60` | rect, two colours and two ints into a 24 byte panel record. 26 callers |
| `FFX_MenuLayout_SlotWord 0x8C2AF0`, `SlotRect 0x8C2BC0` | slot taking variants, reached only from the orphan at 0x8C1AA0 |

There is no index based lookup. Every caller passes a string literal. 262 distinct names are read
from slot 1 and 51 from slot 2, every one of them present in its file. 150 menu values are never
asked for. [V, a ctree sweep of every call to the getters and wrappers]

### 4.2 Scene objects

`g_ffxMenuSceneObjects 0x1865F20` holds 8 objects of 32 bytes per slot (`slot * 0x100`): `+0` u16
state (0 free, 1 start, 2 playing, 3 held, 4 stop), `+2` u16 protected, `+4` u16 hold, `+8` the
name, `+0x1C` the scene entry offset. [V]

| function | does |
|---|---|
| `FFX_MenuScene_Start 0x8AF0D0` | find a free object, `FindScene`, state 1. Refuses a name already present |
| `FFX_MenuScene_StartHeld 0x8AF1B0` | the same with hold, the scene keeps drawing its last frame |
| `FFX_MenuScene_Stop 0x8AF4F0` | state 4 for that name |
| `FFX_MenuScene_StepObjects 0x8AE0F0` | 1 -> `StartScene`, 2 -> 3 when held or -> 4 when finished, 4 -> `StopScene` and free, then compact. `FFX_Module_StepAll` (slot 1), `FFX_BtlMenu_Step` (slot 2) |
| `FFX_MenuScene_DrawGroup 0x8AE2F0(slot, group, noRect)` | `FFX_MenuScript_DrawGroupScene` for every object in state 2 or 3 |
| `FFX_MenuScene_IsDone 0x8AE070`, `GetState 0x8AE530` | the queries. **Slot 2 callers only** |

`FFX_MenuObj_DrawAllAtPrio 0x8A9690(prio)` sets the 512 x 416 clip (`sub_8F9230(0, 0, 512, 416)`)
and calls `FFX_MenuScene_DrawGroupRects(1, prio)`, then draws the menu objects of that same priority.
`FFX_Module_DrawAll` calls it once at 0x8AA34C and eight more times at 0x8AA3A5..0x8AA3D6. So a menu
scene's draw group is its draw priority, and it lands under the menu objects of that priority. [V]

`FFX_MenuScript_RunScene 0xA7B460` is reached only through `DrawGroupScene 0xA7B220` (one caller,
`FFX_MenuScene_DrawGroup`), and it both steps and draws. **A scene advances one frame per draw call,
not per simulation step.** [V, xrefs]

Correction 2026-10-28 (MENU_DRAW_PATH.md 12): "per draw call" is not display rate. The menu draw
call is made from `FFX_MenuSys_StepFrame` inside `FFX_MainStep`, once per menu step, catch-up
steps included, so a slot 1 scene advances per menu step. The battle path `FFX_BtlMenu_DrawRoot`
is called only from `FFX_MesWin_DrawAllWindows`, which `FFX_MainStep` skips on catch-up steps
(0x821533), so a slot 2 scene advances once per non-catch-up step.

`FFX_MenuScene_DrawGroup` has four callers: `FFX_MenuScene_DrawGroupRects` (the menu, slot 1),
`FFX_BtlMenu_DrawRoot` twice and `FFX_BtlOd_WakkaReels_Draw` (slot 2). [V]

One hard coded patch sits in the rect primitive `FFX_MenuScript_DrawRectPrim 0x8FDF20`: when
`FFX_MenuScene_GetState(2, "bwakk")` is 3 (held) and the rect being drawn has y 7 and h 22, it moves
it to y 67 before drawing. `render` does not apply it. [V]

### 4.3 Which scenes run

Slot 1 scenes started by code (9 of 35): `scene0`, `scene11`, `scene20` (`sub_8CF830`, the +0x0C slot
of module 4's descriptor 0xC5A744), `scene2`, `scene3`, `scene33`, `scene34` (the save and load
screen, `sub_8B3680`, `sub_8B5000`, stopped from `FFX_SaveLoadMenu_StepModule`), `scene30` (the world
map marker, `sub_8E2B60`, offset to the save block marker x / 2, y / 2 - 16) and `scene7` (a window,
`sub_8E2F80`, offset to the window position). The other 26 are never started. system_script's 2
scenes are never started. Battle scenes: the battle menu starts only the `lay` layouts
(`bnormallay`, `bnormalllay`, `blimitlay`, `bleftwlay`, `brightwlay`, `bsublay`) and `bclose`, the
overdrive minigames start `btidu`, `bgood`, `bauro`, `bauro2`, `blulu`, `blulu2`, `bgoodl` and `bwakk`,
and the other 19 (`bnormal`, `bleftw`, `brightw`, `bsub`, `blimit` and the rest) are never started and
have no name string in FFX.exe. [V for the start sites, M for the 19, `BATTLE_MENU.md` 5]

## 5. How a screen is built

1. **Load.** The module's loader (often its descriptor's +0x08 slot) calls the getters by name and
   stores the results in its own globals. `menuscript.py screens` lists the loaders and their names.
   `FFX_MenuLayout_LoadCommon 0x8AE6F0` (`sub_8AE6F0`) does the shared block at boot: window colours,
   the nameplate, command, help and place plates, `col_menubg0/1`. [V]
2. **Draw.** The module's draw function places plates and text from those globals, adding code
   constants and per row offsets. Many `pos_` values are offsets inside a plate, not screen
   positions (`pos_cname` 42, 7 sits inside the config nameplate). So the file gives the pieces and
   the code places them. [V for the reads, the per name meaning needs the draw code]
3. **Animate.** Scenes started by name draw extra primitives at their draw priority: cursor bars,
   window frames, the save screen's scroll bar (`scene3` is one rect at 480, 70, 16 x 275).
4. **Units.** Everything is in the PS2 frame, 512 x 416. The rect primitive calls
   `FFX_Sprite2D_Draw(v, 0, 1, 0, 0)`, and the third argument 1 maps PS2 units to 1920 x 1080 as
   `x * 3.75 - 960`, `y * 2.596 - 540`, a non uniform stretch with no pillarbox
   (`TEXT_RENDERING.md` 5). There is no anchor or per resolution table in the file. [V]

**The HD remaster bypasses the file in places.** The main menu draw `sub_8E0BA0` (module 1's +0x04
slot) reads no value from slot 1. It sets `g_ffxMenuNamePlateSpacing 0x25D1670 = 44` and
`g_ffxMenuCommandPlateSpacing 0x25D1640 = 32` (from `num_namepltspc`, `num_compltspc`) every frame,
and lays out with 76 `FFX_UiScaleX/Y` calls on HD constants (rows at y 200 + 90 * i, 1109 wide). Only
the background gradient `col_menubg0/1` comes from the file, via `FFX_Module_DrawAll`. Other screens
mix both: `FFX_Menu_StatusLoadLayout` reads 55 names but its draw helpers also call `FFX_UiScaleX/Y`.
[V for module 1, I for how much of each other screen still comes from the file]

So for 6.4.2, "the main menu matches a screenshot" is a matter of `sub_8E0BA0`'s constants, not of
this file. menu_script.bin is the source for the submenus' plates and for the scene overlays.

## 6. Coverage of the shipped files

From `python -I tools/menuscript.py survey` (exit 0):

```
ok   jppc/system_script.bin  1008 bytes   16 values (9 x1, 3 x2, 4 x4)    2 scenes    5 tracks    5 sequences
ok   jppc/menu_script.bin   18736 bytes  412 values (146 x1, 158 x2, 108 x4)  35 scenes  172 tracks   79 sequences
ok   jppc/battle_script.bin 35584 bytes  191 values (138 x1, 40 x2, 13 x4)   34 scenes  527 tracks  416 sequences
(and the same for the three uspc copies)
6 of 6 files parsed to the last byte
```

Every byte of all six is a header, a bucket, an entry, a name, a sequence, a sequence table, a track
list, a draw group or alignment padding. 3,000 random single bit flips of menu_script either parse
(a flipped coordinate or colour is still a valid file) or raise the parse error, none crash.

## 7. Simulation or presentation

The recreation hashes simulation state every step. Per part:

| part | verdict | evidence |
|---|---|---|
| system_script.bin | **neither, it is dead.** Loaded, never read | no call passes slot 0: every caller of the slot taking library and wrappers passes 1 or 2 or forwards a parameter whose callers do, the orphan variants have no callers, `g_ffxMenuScriptSlots` is read only inside the library, `FFX_MenuRes_GetSystemScriptBuf 0x8AC8B0` has no caller [V] |
| menu_script values | **presentation** | the 281 resolved destinations of the 262 names are read only by draw functions and layout helpers (`FFX_Module_DrawAll`, `FFX_Menu_StatusDrawListPanel`, `sub_8F0A70`, `sub_8D4EE0` and the like). The `num_` values are spacings, scales or stored and never read: `num_cnameplt` lands in `g_ffxMenuConfigNamePlateCount 0x186A48C`, which nothing reads, `num_pmplt`, `num_wpnplt`, `num_staplt` are not read past their loader, `num_face1` is a draw scale in `sub_8D32B0`, `num_name1` a draw offset in `sub_8C4770`. No cursor rule, page size or commit reads one [V for the resolved globals, 53 outputs go to locals or struct pointers and were not traced one by one] |
| menu_script scenes | **presentation** | slot 1 scenes are only started, stopped, offset and drawn. Nothing queries a slot 1 scene's state, `IsDone` and `GetState` have slot 2 callers only [V]. They advance per draw call, which is once per menu step (catch-up included, MENU_DRAW_PATH.md 12), so step rate, not display rate. Still presentation since nothing reads them |
| battle_script values | **reaches simulation** | `dum21` and `dum28` are read by `FFX_BtlOd_PressCircleGate`, `FFX_BtlOd_ArmPressCircleGate` and other overdrive code, `dum225` by the four minigame resets [V for the reads] |
| battle_script scenes | **reaches simulation** | `FFX_BtlOd_TidusSwingBarMinigame` state 2 waits for `FFX_MenuScene_IsDone(2, "btidu")`, state 6 for `"bgood"`, the Auron and Lulu minigames the same. Those scenes advance in `FFX_BtlMenu_DrawRoot` and `FFX_BtlOd_WakkaReels_Draw`, so the minigame's phase timing is set by battle_script.bin and clocked by draw calls [V for the waits and the draw path, the consequence for the battle outcome is item 1.10's to trace] |

One trap for whoever audits this further: `sub_8CA1B0`, an item sort in module 3 that calls
`FFX_SaveData_SwapEquipEntries`, appears to read `p_linmline`'s destination. It does not. It compares
a pointer against `&word_186A402` as the end of an array that sits just before it. [V]

## 8. For the recreation

- The format is small enough to port whole: `menuscript.py` is the reference parser and player. Its
  `Script.play` reproduces `StepTrack`, `DrawTrack`, `DrawKeyframe` and `Ease`, including the op 2
  track 0 bug and the one frame gap a tween leaves when it ends on a non keyframe op.
- Keep the scene state outside the hashed simulation state for slot 1. Keep it inside for slot 2,
  and drive it from the step, not the present.
- Load the jppc menu_script for every language, the game does.
- Do not port system_script.bin.

## 9. Corrections to other docs

- `VBF_FORMAT.md`, the `menu/*_script.bin` skim: the header is not 52 entry points, it is 16 value
  buckets, 16 scene buckets, a sequence table offset, 3 zero words and 16 draw groups. The 4 byte
  shape is `u24 name offset, u8 value count`, not `u16 offset, u8 00, u8 kind`. The name counts are
  447, 18 and 225, not 489, 22 and 211. There are no `kpos_` or `fkpos_` names. battle_script is not
  scene or camera cut names, it is the battle HUD in the same format (`kuro` and `siro` are colour
  values). The game does not load one set per language, it loads jppc for menu and system. Fixed
  there with a pointer here.
- `OVERDRIVES.md` 1.1 route 2 and `MINIGAMES_TIMED.md` 9.5 call `bauro`, `btidu`, `blulu`,
  `blulu2`, `bgoodl`, `bwakk`, `bgood` sound cue strings. They are battle_script.bin scene names,
  started and polled through `FFX_MenuScene_*`. Notes added there. The Lulu attribution is helped,
  not hurt: `blulu` is a 47 track HUD animation in the battle script.
- `FFX_BtlOd_WakkaReels_IsCuePlaying 0x8977C0` and `FFX_BtlOd_IsGoodCuePlaying 0x8977E0` (renamed 2026-10-28 to `FFX_BtlOd_WakkaReels_IsSceneDone` and `FFX_BtlOd_IsGoodSceneDone`) return
  `FFX_MenuScene_IsDone`, which is true when the scene has **finished** (bit 30 clear), so the names
  read the opposite way. Not renamed here because they are plugin ABI rows other docs cite.
- `MENU_SYSTEM.md` and `TEXT_RENDERING.md` 9 are right that sgMenu is not a draw path. PROGRESS
  1.12.8 and 6.4.2 still say "the sgMenu draw path". For the record, the menu draw path that touches
  this file is section 4.2 here.

## 10. Names

`tools/apply_menuscript_names.py`, rehearsed with `--apply --comments` on a copy: 62 functions and
labels, 2 renames of bogus library signature matches (`__cfltcvt_init_150 0xA7BB00` ->
`FFX_MenuScript_ClearSlots`, `?_Get_future_error_what@std@@YAPBDH@Z_16 0xA7B910` ->
`FFX_MenuScript_GetSlotBase`) and 11 globals, 0 skipped, 0 conflicts. The library is
`FFX_MenuScript_*`, the getters `FFX_MenuLayout_*`, the scene objects `FFX_MenuScene_*`. Twelve of
the rows are code with no caller and no function, named with an `_ORPHAN` suffix. `SlotWord` and
`SlotRect` are only reached from one of them, so they are dead too.

## 11. Open

- The per name meaning of the 262 read values (which `pos_` is an offset inside which plate) needs
  each screen's draw code. `render` draws them raw, so a `pos_` drawn at the frame origin is usually
  an offset, not a misplacement.
- `sub_8CF830`'s scenes and module 4: the +0x0C descriptor slot's role is the module table's
  business (item 1.12.13).
- The op 6 shape tables (`sub_8FA7D0`, `sub_8FA450`) are not decoded, so `render` marks a shape with
  a cross and its id, not its size.
- Whether `FFX_BtlMenu_DrawRoot` runs exactly once per battle step, which decides whether the
  overdrive minigame timing is deterministic. Item 1.10 territory.
  Answered 2026-10-28 in MENU_DRAW_PATH.md 12: once per non-catch-up step, never on a catch-up step
  (its only caller chain is `FFX_MainStep` 0x82153C -> `FFX_MesWin_DrawAllWindows` 0x8ABD80, gated
  by `cmp g_ffxIsCatchUpStep, 0` at 0x821533). So the timing is not deterministic across machines
  with different catch-up patterns. `FFX_BtlOd_WakkaReels_Draw` (vtable 0xC65710) is not traced.
