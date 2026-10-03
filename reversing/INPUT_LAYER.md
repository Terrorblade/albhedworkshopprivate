# The input layer

How FINAL FANTASY X HD Remaster reads a controller and a keyboard, where the result lives, who
writes it, when in the frame, and what all of that means for a co-op mod that has to read a second
player, suppress a player who does not have control, and forge a remote player's input.

Addresses below are VAs as IDA shows them. The project convention is RVA + 0x400000 = VA, so
subtract 0x400000 for anything going into `addresses/Input.h`.

The conclusions are in code:

- `loader/workshop/include/ffx/addresses/Input.h` - every address.
- `loader/workshop/include/ffx/Input.h` - the button constants, the singleton layout, the port-state
  layout, the Phyre semantics, and the injection contract.
- `loader/workshop/src/ffx/Input.cpp` - readers, a name formatter, and the writers.

---

## 1. The shape of the thing, in one paragraph

There is one `FFX_Input` singleton at **`0xCCB170`**. It holds a 32-bit button mask, the previous
frame's copy of that mask, 29 hold timers, and four stick floats. One function fills it,
`FFX_Input__poll 0x632A30`, by walking a table of 75 bindings and OR-ing in a PS2-style bit for each
binding whose key or pad button is down. Every physical device, gamepad and keyboard alike, collapses
into that one mask at that one point. Nothing above it takes a port or a device argument.

Below the singleton, the game re-emulates a PS2 `scePad` and feeds **both** emulated ports from the
same singleton, so port 1 is a byte-for-byte clone of port 0. Gameplay reads the emulated ports, not
the singleton. The PC-only code, meaning the Esc menu, the HD boosters and the Iggy Flash UI, reads
the singleton directly.

```
  physical devices
        |
        v
  Phyre, 18 independent pad slots       <-- ffx/Pad.h reads slot 1 here, no hook needed
        |  (FFX only ever reads slot 0)
        v
  FFX_Input__poll  0x632A30             <-- THE COLLAPSE POINT, one mask, no port argument
        |
        v
  g_ffxInput  0xCCB170                  <-- +0 cur, +4 prev, +8 timers, +140 four axis floats
        |                   \
        |                    \---------> FFX_Input__isHeld/isPressed/... : Esc menu, boosters, Iggy
        v
  FFX_Pad__fillSceReadData 0x8898A0     <-- takes a port argument and ignores it
  FFX_Pad__fillAnalogAndSynthDpad       <-- same, plus it synthesises dpad bits from the left stick
        |
        v
  g_ffxPadPortState  0x1330288          <-- 2 ports x 256 bytes, 4 ring slots each
        |            \
        |             \---> FFX_MenuSys_SamplePad, FFX_Atel__samplePadsBothPorts, FFX_Btl_MainStep
        v
  FFX_Player__readPad 0x82D070          <-- hardcoded port 0 slot 0
        |
        v
  g_ffxPlayerPadButtons 0x23C44AC + 3 more, and 4 analogue bytes at 0x23C44A4..A7
        |
        v
  FFX_Player__stepControl 0x82D180      <-- the single controlled character
```

---

## 2. The button mask

**Global: `g_ffxInput + 0` at VA `0xCCB170`, 32 bits.** `FFX_Input__getButtonMask 0x630C80` is
literally `return *(DWORD *)FFX_Input__get();` and `FFX_Input__get 0x631300` is
`return &g_ffxInput;`.

The singleton is referenced by exactly **one** function in the whole binary. `g_ffxInput` has two
data xrefs: `FFX_Input__get`, and `sub_AEEA80` which is the CRT static-destructor registration.
Every reader and every writer in the game goes through that one 6-byte leaf. That matters later.

### 2.1 Layout of the singleton

| offset | type | what |
| --- | --- | --- |
| +0 | DWORD | current level mask |
| +4 | DWORD | previous frame's level mask |
| +8 | float[29] | hold timers in seconds, indexed by bit number 0..28 |
| +0x7C..+0x84 | ? | not examined, do not assume padding |
| +136 | float | when the timers were last advanced |
| +140 | float | left stick X |
| +144 | float | left stick Y, **positive is down** |
| +148 | float | right stick X |
| +152 | float | right stick Y, **positive is down** |
| +156 | int | action-map count, 75 in the shipped build |
| +160 | int | action-map capacity |
| +164 | ptr | action-map data, `{int bindingId, DWORD ps2Mask}[]` |
| +168 | BYTE | last input came from the pad |
| +172 / +176 / +180 | ctx / arg / fn | the poll override triple |

There being exactly **29** hold timers is the independent proof that the mask stops at bit 28.
`FFX_Input__clearHoldTimers 0x631270` zeroes +8 through +120 inclusive, which is 29 dwords, and
`FFX_Input__updateHoldTimers 0x6330E0` loops 29 times shifting a 1-bit mask.

### 2.2 How each bit got its name

This is the part the task asked me not to assume, so here is the chain. None of it comes from a PS2
header.

**Step 1, which bit belongs to which remappable action.** `FFX_Input__buildActionMap 0x6315C0` runs
once from `FFXApplication__initApplication` and appends 75 entries of `{bindingId, ps2Mask}`. Each
entry's `bindingId` is read from the settings block and each `ps2Mask` is an immediate in the code.

**Step 2, which settings slot belongs to which action index.**
`FFX_Config_GetBindingForAction 0x6311B0` is `return settings[action + 24];`, i.e. settings offset
`96 + 4*action`. That is the formula the options UI edits through, and it matches the offsets
`buildActionMap` reads, so entry index N in the action map is action index N.

**Step 3, each action's shipped default.**
`FFX_Config_GetDefaultBindingForAction 0x402BE0` is `return g_defaultKeyBindingsFFX[action];`, with
the table at **`0xC8A538`**. For keyboard actions the value is a `PInputKeySemantic` key code, which
`FFX_Input_KeyCodeToName 0x405AF0` can turn into a string via `g_keyCodeNames 0xC8ABE0`. For the pad
actions, 14 through 27, the value is a `PInputJoypadButtonSemantic`.

**Step 4, what a Phyre joypad semantic physically is.**
`Phyre__PInputDevicePadXInput__poll 0x62DBA0` writes one byte per semantic at `device+920+(sem-11)`
straight out of `XINPUT_GAMEPAD.wButtons`, and the XInput bit values are fixed and public. So:

| semantic | XInput source | identity |
| --- | --- | --- |
| 11 | `0x4000` X | left face button = square |
| 12 | `0x1000` A | bottom face button = cross |
| 13 | `0x2000` B | right face button = circle |
| 14 | `0x8000` Y | top face button = triangle |
| 15 | `0x0100` LEFT_SHOULDER | L1 |
| 16 | `0x0200` RIGHT_SHOULDER | R1 |
| 17 | `bLeftTrigger != 0` | L2, **digital** |
| 18 | `bRightTrigger != 0` | R2, **digital** |
| 19 | `0x0020` BACK | select |
| 20 | `0x0010` START | start |
| 21 | `0x0040` LEFT_THUMB | L3 |
| 22 | `0x0080` RIGHT_THUMB | R3 |
| 23 | `0x0001` DPAD_UP | up |
| 24 | `0x0008` DPAD_RIGHT | right |
| 25 | `0x0002` DPAD_DOWN | down |
| 26 | `0x0004` DPAD_LEFT | left |

Note that Phyre throws the analogue trigger value away. **There is no trigger pressure anywhere in
this game on PC**, L2 and R2 are on/off.

**Putting steps 1 to 4 together.** Pad actions 14..27 default to semantics
23, 25, 26, 24, 13, 12, 14, 11, 15, 17, 16, 18, 20, 19 and `buildActionMap` pairs those same actions
with bits `0x1000, 0x4000, 0x8000, 0x2000, 0x20, 0x40, 0x10, 0x80, 0x04, 0x01, 0x08, 0x02, 0x800,
0x100`. That gives:

| bit | mask | button | from semantic |
| --- | --- | --- | --- |
| 0 | `0x00000001` | L2 | 17 |
| 1 | `0x00000002` | R2 | 18 |
| 2 | `0x00000004` | L1 | 15 |
| 3 | `0x00000008` | R1 | 16 |
| 4 | `0x00000010` | Triangle | 14 |
| 5 | `0x00000020` | Circle | 13 |
| 6 | `0x00000040` | Cross | 12 |
| 7 | `0x00000080` | Square | 11 |
| 8 | `0x00000100` | Select | 19 |
| 9 | `0x00000200` | L3 | nothing binds it, see below |
| 10 | `0x00000400` | R3 | nothing binds it, see below |
| 11 | `0x00000800` | Start | 20 |
| 12 | `0x00001000` | Up | 23 |
| 13 | `0x00002000` | Right | 24 |
| 14 | `0x00004000` | Down | 25 |
| 15 | `0x00008000` | Left | 26 |

This is the PS2 `libpad` layout, but it was derived, not copied. The agreement is a cross-check, not
the evidence.

**Three further checks on the directions**, each of which stands on its own:

1. `FFX_Pad__fillAnalogAndSynthDpad 0x889570` synthesises the dpad from the left stick byte:
   `LX < 0x10` sets `0x8000`, `LX > 0xF0` sets `0x2000`, `LY < 0x10` sets `0x1000`,
   `LY > 0xF0` sets `0x4000`. Far left is `0x8000`, so `0x8000` is Left.
2. `FFX_Pad__fillSceReadData 0x8898A0` refuses simultaneous opposites:
   `if ((m & 0xA000) == 0xA000) m &= 0x5FFF;` then `if ((m & 0x5000) == 0x5000) m &= 0xAFFF;`.
   So `(0x8000, 0x2000)` is one axis and `(0x1000, 0x4000)` is the other.
3. `FFX_Input__readUiNavEvent 0x6EF320`, which I renamed from `sub_6EF320`, treats
   `isHeld(0x1000)` as the same navigation event as `leftStickY < -0.3`, `0x4000` as
   `leftStickY > +0.3`, `0x8000` as `leftStickX < -0.3` and `0x2000` as `leftStickX > +0.3`.

**L3 and R3 are dead.** Both have a bit position and a hold timer, but no entry in the action map
sets either one, so the PC poll can never produce them. `FFX_PlayerCam_ReadPadInput 0x83F4B0` still
tests `g_ffxPlayerPadPressed & 0x400` to cycle a debug camera distance through 50, 100, 180, 250,
1000, 30. That code is unreachable in the retail build.

### 2.3 The PC-only high bits

Bits 16 through 28 are real bits of the same mask. The scePad layer stores only the low 16, so
gameplay never sees them, but the PC UI does.

| mask | action | default key | what it does |
| --- | --- | --- | --- |
| `0x00010000` | 28 | F1 | booster: cycle game speed 1x / 2x / 4x, `FFX_Booster_UpdateSpeedHotkey` |
| `0x00020000` | 29 | F4 | booster: auto battle, `FFX_Booster_PollAutoBattleKey` |
| `0x00040000` | 30 | unbound | nothing binds it by default, tested in `sub_680030` |
| `0x00080000` | 31 | F5 | tested in `FFX_Frame_UpdateBoostersAndOverlays` |
| `0x00100000` | 32 | unbound | tested in `FFX_Frame_UpdateBoostersAndOverlays` |
| `0x00200000` | 33 | F2 | booster: invincible, `FFX_Booster_PollInvincibleKey` |
| `0x00400000` | 34 | F3 | booster: no encounters, `FFX_Booster_PollEncounterRateKey` |
| `0x00800000` | - | - | **no action sets this.** `isPressed(0x800000)` at `0x6F760F` is dead code |
| `0x01000000` | 40 | Esc | the pause / Esc menu key |
| `0x02000000` | 36 | Home | R1 marker |
| `0x04000000` | 37 | End | R2 marker |
| `0x08000000` | 38 | A | L1 marker |
| `0x10000000` | 39 | Q | L2 marker |

The four markers are the interesting ones. A **pad** shoulder press sets its PS2 bit *and* its
marker: the pad action for L1 carries mask `0x08000004`, L2 `0x10000001`, R1 `0x02000008`,
R2 `0x04000002`. A **keyboard** shoulder binding sets only the marker. So the HD UI can page with
L1/R1 without colliding with the 16-bit scePad space, and it does: `sub_89ECE0` tests
`isHeld(0x02000000)` and `isHeld(0x04000000)`, `sub_89C900` tests `isPressed(0x08000000)`, and
`sub_8A2160` tests `isRepeating(0x08000000)` and `isRepeating(0x10000000)`.

### 2.4 What I could NOT establish

**Which bit means "confirm".** The bit *identities* are settled, but the gameplay meaning is not,
and the game's own defaults pull in two directions. The primary keyboard defaults look mnemonic:
`X` key to the Cross bit, `C` key to the Circle bit. The alternate keyboard defaults, actions 49..62,
bind only two keys, and they bind **Enter to the Circle bit `0x20`** and **Backspace to the Cross bit
`0x40`**. If Enter is confirm, confirm is Circle. If Cross is confirm, Enter is cancel. I did not
find a site that settles it. `FFX_Input__readUiNavEvent` returns event 7 for Circle and event 8 for
Cross with no names attached, and the options-screen labels come from numeric message ids resolved
out of a data file rather than from strings in the exe, so I could not read the UI text either.

This does not block anything: a co-op mod forwards bits, it does not need to know which one is
confirm. But do not take a guess from this document and put it in a comment.

---

## 3. The analogue sticks

Four floats at `g_ffxInput + 140`, in the order **LX, LY, RX, RY**.
`FFX_Input__getAnalogAxes 0x630C90` returns a pointer to the first.

**Range** is about -1.004 to 1.0. Phyre computes `value = raw / 255.0` where `raw` is the XInput
thumb divided by 128, and DirectInput devices have `DIPROP_RANGE` set to -255..255 in `sub_6233A0`,
so the two backends agree.

**Sign.** X is positive right. **Y is positive DOWN.** `Phyre__PInputDevicePadXInput__poll` stores
`sThumbLY / -128`, and `FFX_Input__readUiNavEvent` confirms it at the consumer end by treating
`leftStickY < -0.3` as Up. This matches `ffx::PadState` in `ffx/Pad.h`, which already carries the
same warning.

**Deadzone.** A 0.2 deadzone has already been applied per axis by
`Phyre__PInputMapper__getPadAxis 0x628470`, with a cross-axis relaxation: if the other axis of the
same stick is outside its own deadzone, this axis gets a deadzone of zero. So a diagonal push reports
small values honestly while a pure single-axis nudge is snapped to zero. For an undeadzoned value,
`Phyre__PInputDevicePad__getAnalogChannel 0x628AD0` is `__thiscall(device, semantic)` with semantic
1 = LX, 2 = LY, 4 = RX, 5 = RY and returns `raw / 255.0`, or read the raw ints at `device+880` LX,
`+884` RX, `+892` LY, `+896` RY.

**Downstream.** `FFX_Pad__axisFloatToByte 0x889AA0` is `0x80 - (int)(f * -127.0)`, so each float
becomes a byte centred on `0x80`. `FFX_Pad__fillAnalogAndSynthDpad` writes them to
`portState+132 = RX`, `+133 = RY`, `+134 = LX`, `+135 = LY`, and `FFX_Pad__commitRingSlot` copies
`portState+132..+147` into ring slot `+12..+27`. `FFX_Player__readPad` then pulls ring indexes 0..3
into `g_ffxPlayerAnalogRX 0x23C44A7`, `RY 0x23C44A6`, `LX 0x23C44A5`, `LY 0x23C44A4`. Note the
globals sit in one dword in the order LY, LX, RY, RX, which is easy to get backwards.

**How movement consumes them.** `FFX_Player__stepControl 0x82D180`:

- Deadzone again: LX is snapped to 128 when `89 <= v <= 167`, LY likewise.
- **If any dpad bit is set (`mask & 0xF000`), the stick is ignored entirely** and four digital
  ramps (`g_ffxPlayerDirRampUp/Down/Left/Right`, step 32, clamp 256) drive the heading.
- Otherwise it builds `2*stickLY - 256` and `2*(128 - stickLX)`, applies a 96.0 threshold, and
  `atan2`s them into `g_ffxPlayerDesiredHeading 0x13007A0`, then calls `FFX_Ch_SetMoveSpeed` and
  `FFX_Ch_SetMoveDir` on `g_ffxControlledChr 0x1300788`.

There is a trap in that pair of facts. `fillAnalogAndSynthDpad` sets a dpad bit once a stick byte
passes `0x10` or `0xF0`, which is full deflection, and `stepControl` ignores the stick as soon as any
dpad bit is set. So **a fully deflected injected stick takes the digital ramp path and a partly
deflected one takes the analogue path.** If you inject axes and nothing moves, check whether you
also need to clear the synthesised dpad bits, and vice versa: if you inject only mask bits and leave
the axes at zero, the remote player will move but only in eight directions.

---

## 4. Who writes the mask, and when in the frame

`Phyre__PApplication__frameTick 0x627940` is the per-frame driver, installed as the app frame
callback by `onInit`. In order:

1. vtable **+0x24** (slot 9), a pre-update.
2. `Phyre__PInputMapper__latchDeviceStates`. All 18 Phyre pad slots are latched here. This is where
   physical hardware state becomes visible to anything.
3. vtable **+0x1C**, `FFXApplication__update 0x42F770`. Calls `FFX_Input__updateFromFFXApp 0x630E80`,
   which calls `FFX_Input__poll` and fills the mask, the previous mask and the four axes. **Only
   when `g_ffxThreadedPadMode 0x133C930` is 0, and only when the Esc menu is closed.**
4. vtable **+0x10**, `FFXApplication__animate 0x42F520`. Runs `FFX_MainStepLoop 0x822840`, which runs
   `FFX_MainStep`, which calls `FFX_Pad__updateAll` and `FFX_Pad__commitAllPorts` to fill the
   emulated ports, then `FFX_Player__readPad`, then the menu system, the event VM and battle.
   **Everything that reads input runs inside this slot.**
5. render, then `endFrame`.

So the per-frame hook the Workshop already uses, `FFXApplication::animate`, vtable `+0x10`, RVA
`0x0070D9A8`, is exactly the right place. **Writing the mask from a detour on that slot, before
calling the original, lands after the latch and before every reader.** That is the answer to the
question.

Two things spoil it, and both are checkable:

- **`g_ffxThreadedPadMode != 0`.** Then `FFX_Input__poll` runs on `FFX_InputThread__main` at 60 Hz
  and pushes samples into a queue, and `FFX_Input__consumeThreadedSample 0x630DE0` pops one into the
  singleton at the **top of `FFX_MainStepLoop`**, which is inside animate and runs once per
  catch-up substep. A write made before animate is overwritten before anything reads it. The flag is
  set to 1 at `0x891C7E` and back to 0 at `0x891FD8`, both inside the state machine `sub_891B80`; I
  did not determine which path the retail build takes at boot, so **a mod must read the flag at run
  time rather than assume**. `ffx::ThreadedPadMode()` exists for exactly that and
  `ffx::LogInputState()` prints a warning when it is non-zero.
- **The Esc menu is open** (`FFXApplication+941 != 0`). Then `update` skips the poll entirely and
  `animate` calls `FFX_Input__pollForMenu 0x644990` **first**, which re-polls. Same outcome.

### 4.1 The hook that is immune to both

Both paths go through `FFX_Input__poll`, and the game ships a supported way to take it over. The
triple at `g_ffxInput+172/+176/+180` is installed by `FFX_Input__setPollOverride 0x630DF0`, and at
the very top of `poll`:

```c
    fn = singleton[45];                    // +180
    if (fn) {
        skip = 0;
        fn(singleton[43], singleton[44], &skip);   // +172 ctx, +176 arg
        if (skip) goto done;               // done: clearHoldTimers(); return;
    }
```

When `skip` comes back non-zero the whole poll is abandoned without touching cur, prev or the axes.
The only side effect on that path is `FFX_Input__clearHoldTimers`, which zeroes the 29 timers and
refreshes the timestamp, so a mod that owns the poll also owns auto-repeat and has to do its own.
`FFX_Input__swapPollOverride 0x632930` additionally zeroes cur and prev when the triple is installed
or removed, so expect one blank frame at the transition.

In the shipped binary the only user of this is the key-and-pad rebinding UI around `0x701190`.
Nothing installs it during normal play, so it is free for a mod to take.

---

## 5. Edge versus level

Yes, the game keeps a previous-frame mask, and it keeps **three** separate edge caches. A co-op layer
that injects only the level state will produce stuck and repeated presses, exactly as suspected.

**Cache 1, the singleton.** `g_ffxInput+4` is last frame's mask. `FFX_Input__poll` ends with
`prev = cur; cur = newMask;`. The accessors:

```c
    maskIsHeld      = (m & cur) != 0
    maskIsPressed   = (m & prev) == 0 && (m & cur) != 0
    maskIsReleased  = (m & prev) != 0 && (m & cur) == 0
```

`FFX_Input__maskIsRepeating 0x6313A0` adds auto-repeat on top: it returns true on the rising edge,
and otherwise scans the 29 hold timers for a bit in `m` whose timer has passed **0.2333 s**, then
resets that timer to **0.1333 s**. So the first repeat comes after 0.233 s and subsequent ones every
0.133 s. `FFX_Input__updateHoldTimers 0x6330E0` advances a timer while its bit is held and zeroes it
otherwise, using wall-clock deltas from `FFX_Input__getTimeSeconds 0x630C60`.

**Cache 2, the emulated scePad ring slots.** Each ring slot carries `+4` level, `+6` pressed,
`+8` released, read by `FFX_Pad__readButtons16 0x888D70`, `readPressed16 0x888E80` and
`readReleased16 0x888EA0`. The menu system, the atel event VM and battle all read these. There are 4
ring slots of 32 bytes per port, sitting at the front of the port state, and the `ring` argument
those three readers take is **relative to the current write index**, not absolute:
`FFX_Pad__getRingSlot 0x888B60` is `portState + 32 * ((ringArg + portState[156]) & 3)`, so 0 is this
frame and -1 is last frame. `FFX_Atel__samplePadsBothPorts 0x871D70` OR-s the whole history together
so a press cannot be missed between event-script steps, and `FFX_MenuSys_SamplePad` OR-s slots 0 and
-1 for the same reason.

**Cache 3, the player globals.** `FFX_Player__readPad` computes
`pressed = cur & (cur ^ prev); prev = cur;` into `g_ffxPlayerPadPressed 0x23C44A8` and
`g_ffxPlayerPadPrev 0x23C44B0`. Watch out for `FFX_Player__getPadButtons 0x82D050`, which **clears
`g_ffxPlayerPadPressed` as a side effect**, so the first caller in a frame consumes the edge for
everybody.

**The useful part:** caches 2 and 3 are derived from cache 1 every frame, by code that runs inside
animate. So writing cache 1 before animate drives all three consistently. That is the practical
reason the pre-animate slot is the right place, and the reason `ffx::WriteButtonMask` takes a
`prevMask` argument instead of just a mask.

---

## 6. Keyboard

**The keyboard merges into the same mask.** There is no separate keyboard path to suppress. It is all
one action map walked by one function, and the only difference between keyboard and pad is which
block of the table an entry sits in and which Phyre query is used on it,
`Phyre__PApplication__isKeyDown 0x629D50` versus `isPadButtonDown 0x629CA0`.

The 75 entries, with the byte offsets `FFX_Input__poll` iterates by:

| entries | offsets | device | settings slots | when tested |
| --- | --- | --- | --- | --- |
| 0..13 | 0..111 | keyboard, primary | +96..+148 | only if the pad block produced nothing **and** LX and LY are both exactly 0.0 |
| 14..27 | 112..223 | **pad, index 0** | +152..+204 | always |
| 28..40 | 224..327 | keyboard, PC extras | +208..+256, and +544 for entry 31 | always |
| 41..48 | 328..391 | - | +908..+936, all masks 0 | **never walked by poll** |
| 49..62 | 392..503 | keyboard, alternate | +292..+344 | same pad-gave-nothing rule |
| 63..70 | 504..567 | keyboard, PC extras alt | +348..+376 | always |
| 71..74 | 568..599 | keyboard, marker alt | +380..+392 | **never walked by poll** |

So the device precedence is: pad always wins, and the two keyboard movement blocks are consulted only
when the pad produced literally nothing, no bits and both stick axes bit-exactly zero. Note it is the
whole block that is gated, not individual bindings, so one pad button held suppresses the entire
keyboard for movement and face buttons that frame. The PC extras, F1 to F5 and Esc, are never gated.

`g_ffxInput+168`, `LastInputWasPad`, is set to 1 by the pad block and cleared by the keyboard blocks,
which is what the HD UI uses to pick between pad glyphs and key names.

Two smaller keyboard details:

- **Modifier aliasing.** `FFX_Input__poll` opens with a loop over `g_ffxKeyAliasTable 0xC32270`,
  4 rows of `{srcKey, dstKey, latchByte}`, and calls `Phyre__PApplication__setKeyDown` on the
  destination while the source is down: 122 -> 119 Alt, 120 -> 117 Control, 121 -> 118 Shift,
  115 -> 77 Enter. The right-hand modifiers and the numpad Enter act as the main ones.
- **The FMV / modal branch.** When a movie is playing and neither the Esc menu nor
  `maybe_g_ffxInputModalGate` is set, `poll` takes a short path that tests only 11 fixed keyboard
  entries, all of them the PC extras, and then stores the mask and clears the hold timers. So during
  an FMV the pad is ignored entirely and only the booster keys and Esc work. Separately, when
  `maybe_g_ffxInputModalGate` is non-zero, `poll` masks out the 7 PC-extra bits
  `0x10000` through `0x400000` at the end.

### 6.1 Two shipped oddities found on the way

- **Entries 71..74 are dead.** They are the *alternate* keyboard bindings for the four shoulder
  marker bits, and `FFX_Input__poll` never walks offsets 568..599. The options UI shows them
  (`FFX_EscMenu_BuildKeyboardPage` rows 12..15 list alt actions 73, 74, 71, 72) and lets the player
  set them, and they do nothing.
- **Entry 31 reads the wrong settings slot.** `FFX_Config_GetBindingForAction` puts action 31's
  binding at settings `+220` decimal, which is `+0xDC`, and that is what the UI edits.
  `buildActionMap` reads `[eax+220h]` at `0x631DE1`, which is `+544` decimal. Every neighbouring
  entry reads the right slot. That looks like a decimal-for-hex typo in the original source, so the
  F5 action's primary binding is not actually remappable. Its alternate, entry 66, reads `+360`
  correctly, so remapping the alternate works. I did not verify this at run time, so treat it as a
  strong reading of the code rather than a confirmed behaviour.

---

## 7. Every single-player assumption

All of these are marked in the IDB with a comment beginning `CO-OP HAZARD:`, so
`grep`-ing the database for that string gives the live list.

**The root cause.** `FFX_Input__get 0x631300` is the only reference to `g_ffxInput` in the binary.
It is a 6-byte leaf with no notion of who is asking. That makes it simultaneously the worst
single-player assumption in the game and the single best lever available: a detour there that returns
a per-player shadow copy redirects the entire `FFX_Input` layer without touching a single caller.
The cost is that the mod has to know which player's turn it is, which it can, because the co-op layer
is the thing driving the per-character step.

**Collapse points, no port argument at all:**

| function | note |
| --- | --- |
| `FFX_Input__getButtonMask 0x630C80` | one global mask for both emulated ports |
| `FFX_Input__getAnalogAxes 0x630C90` | one global set of four axis floats for both ports |
| `FFX_Input__poll 0x632A30` | pad index hardcoded 0 at the button loop and all four `getPadAxis` calls |
| `FFX_Input__readUiNavEvent 0x6EF320` | reads the global singleton, no notion of player |

**Ports accepted and ignored:**

| function | note |
| --- | --- |
| `FFX_Pad__fillSceReadData 0x8898A0` | takes port and slot, ignores them when fetching the data |
| `FFX_Pad__fillAnalogAndSynthDpad 0x889570` | same |

These two are the single best place to make the game genuinely two-player *inside its own
abstraction*, because the two-port API above them already exists and is already called:
`FFX_Atel__samplePadsBothPorts 0x871D70` reads both ports, so does
`maybe_FFX_Yonishi__feedPadStateBothPorts 0x7FF680`, and `FFX_Btl_MainStep` reads port 0 twice.
Make the fill side port aware and the event VM becomes two-player with no further change.

**Single-slot globals:**

| global | VA | read by |
| --- | --- | --- |
| `g_ffxPlayerPadButtons` | `0x23C44AC` | `stepControl`, `PlayerCam_ReadPadInput`, `getPadButtons` |
| `g_ffxPlayerPadPressed` | `0x23C44A8` | `PlayerCam_ReadPadInput`, `FFX_MenuSys_PollOpenAndStep` |
| `g_ffxPlayerPadPrev` | `0x23C44B0` | `readPad`, `getPadButtons` |
| `g_ffxPlayerAnalogLY/LX/RY/RX` | `0x23C44A4..A7` | `stepControl` (L), `PlayerCam_ReadPadInput` (R) |
| `g_ffxPlayerStickLX/LY` | `0x13007A8`, `0x13007AC` | post-deadzone, written by `stepControl` |
| `g_ffxPlayerDirRamp*` | `0x130078C..0x1300798` | `stepControl`, shared ramp state |
| `g_ffxMenuPadBlock` | `0x25D09C0` | the whole menu system, 0xC0 bytes, one copy |
| `g_ffxAtelPadPort0/1Buttons` | `0x13270C0`, `0x13270C4` | the atel event VM |

**Functions that consume them with no player parameter:**

| function | why it blocks co-op |
| --- | --- |
| `FFX_Player__readPad 0x82D070` | hardcoded port 0 slot 0 into the one set of globals |
| `FFX_Player__stepControl 0x82D180` | one mask, one stick, one `g_ffxControlledChr`, one set of ramps |
| `FFX_PlayerCam_ReadPadInput 0x83F4B0` | one right stick into one camera |
| `FFX_Player__getPadButtons 0x82D050` | one global, and it clears the pressed mask for everybody |
| `FFX_MenuSys_SamplePad 0x8BE500` | the single input read for the entire menu system |

The ramps deserve a specific warning. Even if the mask were made per character,
`g_ffxPlayerDirRampUp/Down/Left/Right` are four shared ints that `stepControl` advances by 32 per
call and clamps at 256. Two characters stepped through `stepControl` in the same frame would smear
each other's heading. A co-op layer driving a second character should either call
`FFX_Ch_SetMoveDir` and `FFX_Ch_SetMoveSpeed` itself rather than going through `stepControl`, or
save and restore the four ramps around each call.

---

## 8. Verdict

**Can a mod cleanly suppress and inject input on this game? Yes, and more cleanly than expected.**

The reason is that FFX funnels everything through one singleton that one function hands out. There is
no scatter of per-subsystem input state to chase down, and the three edge caches are all derived from
the singleton every frame by code inside a single vtable slot. Suppression is two dword stores and
four float stores. Injection is the same stores with different values.

**The best hook point depends on how much you want to be immune to:**

- For normal play, a detour on `FFXApplication::animate`, vtable `+0x10`, RVA `0x0070D9A8`, writing
  the mask and the axes **before** calling the original. That is the hook the Workshop already has
  and it is in the right slot. `ffx::WriteButtonMask` and `ffx::WriteSticks` are built for it.
- For complete coverage, install the engine's own poll override at `g_ffxInput+180` via
  `FFX_Input__setPollOverride 0x630DF0`. That intercepts both the main-thread and the threaded poll
  and both the gameplay and the Esc-menu path, at the cost of maintaining the previous mask and the
  29 hold timers yourself.
- For a genuinely two-player version of the game's own abstraction, make
  `FFX_Pad__fillSceReadData` and `FFX_Pad__fillAnalogAndSynthDpad` port aware. The two-port API above
  them already exists and is already called.

**The biggest unknown left is `g_ffxThreadedPadMode`.** It decides whether the mask is latched
before animate or inside it, and therefore whether the simple hook works at all. The flag is set to 1
and to 0 from two sites in one state machine, `sub_891B80`, and I did not establish which path the
retail build takes at boot or whether it changes mid-session, for example when a movie starts. Any
mod must read the flag at run time, not assume it. `ffx::ThreadedPadMode()` and
`ffx::LogInputState()` make that one line, and the first thing to do with a running build is log it.

Second biggest: **the stick-to-dpad synthesis interacting with `stepControl`'s dpad precedence**
(section 3). It is understood on paper but not tested, and it determines whether injected movement
comes out analogue or eight-way.
