# FFX message boxes and choice prompts, notes for co-op

All addresses are VA. Base is 0x400000, so RVA + 0x400000 = VA.
Everything here was read out of Hex-Rays pseudocode or disassembly unless a line says it is
inference. Section 9 lists what is still unsettled.

Entry point for this work was `ffx::InteractionBlockedKind()` in the kit, which wraps
`FFX_Atel_MesWinBlockingKind` **0x86C8B0** (RVA 0x46C8B0). That function turned out to be the
doorway into the whole layer.

## 1. There are four stacked object arrays, and all four are 8 slots wide

A "message window" is not one object. It is four parallel arrays, all indexed by the same
**window index 0..7**, which the SCRIPT chooses as the first argument to every message syscall.

| VA | name | what it is |
|---|---|---|
| 0x1326D70 | `g_ffxAtelMesWinRecords` | the ATEL-side record, 44 bytes, what scripts write |
| 0x1865B3C | `g_ffxMesWinObjects` | the window shell, holds the **input focus** flag |
| 0x18676F0 | `g_ffxMesWinTextObjects` | the text/typewriter object, 0x130 bytes |
| 0x1868A90 | `g_ffxMesWinChoiceObjects` | **the choice object, 0x138 bytes. The answer lives here** |
| 0x1876DA8 | `g_ffxMesWinFrameObjects` | the box frame under the text, owns the lifecycle state |

All of them are double banked. **Bank 0 is the field set, bank 1 is the in-game menu set.**
`FFX_MesWin_SelectObjectBank` **0x8AF5C0** repoints the last four arrays, and
`g_ffxAtelMesWinBufSel` **0x1326B82** selects the 352-byte stride for the first.
`FFX_MenuSys_StepFrame` **0x8A9CA0** flips both on entering and leaving the menu.

**The bank is a MODE selector, not a player selector.** It does not give a second player its own
windows.

Reach a record through `FFX_Atel_GetMessageWindow` **0x86BE90** (RVA 0x46BE90), which applies
`+ 352 * g_ffxAtelMesWinBufSel + 44 * clamp(index, 0, 7)`. A mod must not index the array
directly.

### 1.1 The ATEL record, 44 bytes

| off | type | field |
|---|---|---|
| +0 +2 +4 +6 | s16 | x, y, w, h |
| +8 +12 | ptr | message text pointer, and the reset copy |
| +16..+19 | u8 | four frame-style bytes handed to `FFX_MesWin_SetFrameStyle` |
| **+20** | **s16** | **STATE.** 0 idle, 1 shown, 2 waiting on the player, 3 closing, 4 finished |
| **+22** | **s16** | **message attribute.** low nibble = digit count, **HIGH BYTE = OPTION COUNT** |
| +24 | u32 | text colour / flags |
| +28 | u8 | frame skin selector |
| **+29** | **u8** | **ATTRIBUTE BITS**, see below |
| +30 +31 | u8 | initial cursor index, cancel cursor index |
| +32 | u8 | window palette |
| +33 | u8 | picks required. `FFX_AtelOp_MesWinShow` **hardwires this to 1** |
| +34 +35 | u8 | initial digit cursor, cancel digit cursor |
| +36 | u32 | numeric entry default value |

Record+29 bits, and this is where the box kind is decided:

| bit | meaning |
|---|---|
| 0x02 | **list choice window**, drives `FFX_MesWin_BeginChoiceList` |
| 0x08 | explicit w/h geometry rather than auto-size |
| 0x10 | plain wait-for-confirm box, set by Core 124 and Core 132 |
| 0x20 | choice wait armed, set by Core 125 start |
| 0x40 | **numeric entry window**, drives `FFX_MesWin_BeginNumericEntry` |

`FFX_Atel_MesWinBlockingKind` returns **2** when any non-idle window has 0x22 set (a choice), **1**
when it has 0x10 set or a non-zero attribute, and **0** when nothing is up.

### 1.2 The choice object, 0x138 bytes. This is the answer

| off | type | field |
|---|---|---|
| +1 | s8 | **state.** 0 not armed, 1 player is choosing, **2 player has CONFIRMED** |
| **+2** | **s8** | **CURRENT CURSOR INDEX. This is the selection while the player is choosing** |
| +3 | s8 | cancel index, copied into +2 on a cancel press |
| +4 | s8 | first selectable index (wrap low) |
| +5 | s8 | option count (wrap high) |
| +6 | s8 | picks required, always 1 in practice |
| +7 | s8 | picks made so far. when it reaches +6, state goes to 2 |
| +8 | s16 | widest option in pixels |
| +12 | s32 | numeric entry value, decimal-split for display |
| +16 | s32 | numeric entry default, restored on cancel |
| **+24..** | **u8[]** | **the answer array, one byte per pick. byte 0 is THE answer** |
| +56 + 8*i | s16 x4 | per option: width, height, x, y of its hit box |

## 2. The message window subsystem, function by function

| VA | RVA | name | what |
|---|---|---|---|
| 0x8AB910 | 0x4AB910 | `FFX_MesWin_StepAll` | the whole per-step update, called from `FFX_MainStep` |
| 0x8B7CD0 | 0x4B7CD0 | `FFX_MesWin_SamplePadPort0` | samples the shared menu pad, **port 0 hardcoded** |
| 0x8B5850 | 0x4B5850 | `FFX_MesWin_RunStepHandler` | runs textObject+276, the typewriter |
| 0x8B6B70 | 0x4B6B70 | `FFX_MesWin_RunConfirmHandler` | runs textObject+252, the input handler. gated on textObject+44 == 2 |
| 0x8B64A0 | 0x4B64A0 | `FFX_MesWin_OnConfirmPressed` | default input handler for a plain box, page advance |
| 0x8B8CA0 | 0x4B8CA0 | `FFX_MesWin_AdvanceText` | what the page-advance press does |
| 0x8ADE10 | 0x4ADE10 | `FFX_MesWin_Activate` | shows the draw object |
| 0x8AB8F0 | 0x4AB8F0 | `FFX_MesWin_Deactivate` | hides it |
| 0x8ADA30 | 0x4ADA30 | `FFX_MesWin_ResetDrawState` | clears the shell and installs the default input handler |
| **0x8AF7D0** | **0x4AF7D0** | **`FFX_MesWin_SetInputFocus`** | **clears objects[i]+6 on all 8, sets it on one** |
| 0x8B0020 | 0x4B0020 | `FFX_MesWin_BeginChoiceList` | installs `FFX_MesWin_ChoiceListInput` |
| 0x8AF730 | 0x4AF730 | `FFX_MesWin_BeginNumericEntry` | installs `FFX_MesWin_NumericEntryInput` |
| **0x8B64E0** | **0x4B64E0** | **`FFX_MesWin_ChoiceListInput`** | **the choice cursor and confirm. reads the MENU pad** |
| 0x8B62B0 | 0x4B62B0 | `FFX_MesWin_NumericEntryInput` | the digit widget. same two menu pad globals |
| 0x8B5FB0 | 0x4B5FB0 | `FFX_MesWin_DrawChoiceCursorAndArm` | **where a choice arms, state 0 -> 1** |
| 0x8B5DC0 | 0x4B5DC0 | `FFX_MesWin_DrawWindow` | per-window draw, the only caller of the above |
| 0x8ABD30 | 0x4ABD30 | `FFX_MesWin_DrawAllWindows` | the field draw pass, inside `FFX_MainStep` |
| 0x8AB580 | 0x4AB580 | `FFX_MesWin_ArmPadSampler` | the bare `g_ffxMenuPadBlock = x` setter |
| 0x8B9A60 | 0x4B9A60 | `FFX_MesWin_LayoutMessageAndFindChoices` | finds the choice markers in the text |
| 0x8B8880 | 0x4B8880 | `FFX_MesWin_ChoiceGridSetup` | sets option count and per-option rectangles |
| 0x8B8210 | 0x4B8210 | `FFX_MesWin_ClearTextAndChoiceObjects` | memsets both objects |
| 0x8B89D0 | 0x4B89D0 | `FFX_MesWin_SetTextSource` | points the text object at the message bytes |
| 0x8B8BB0 | 0x4B8BB0 | `FFX_MesWin_ApplyAttrToTextObject` | attribute to text-object flags |
| 0x8B7FB0 | 0x4B7FB0 | `FFX_MesWin_GetChoiceState` | -> choiceObject+1 |
| 0x8B7FD0 | 0x4B7FD0 | `FFX_MesWin_GetChoicePickArray` | -> &choiceObject[24] once state == 2, else 0 |
| 0x8B7FF0 | 0x4B7FF0 | `FFX_MesWin_GetChoicePickCount` | -> choiceObject+7 once state == 2, else -1 |
| 0x8B8010 | 0x4B8010 | `FFX_MesWin_GetChoiceCursor` | -> choiceObject+2, or -1 |
| 0x8B7C40 | 0x4B7C40 | `FFX_MesWin_GetNumericValue` | -> choiceObject+12 |
| 0x8B7CA0 | 0x4B7CA0 | `FFX_MesWin_GetTextPhase` | -> textObject+46. 3 means the message finished drawing |
| 0x906C20 | 0x506C20 | `FFX_MesWin_GetFrameState` | -> frameObject+8, the lifecycle state |
| 0x906C40 | 0x506C40 | `FFX_MesWin_GetFrameStyle` | -> frameObject+4 |
| 0x906BD0 | 0x506BD0 | `FFX_MesWin_StepFrame` | runs the frame object's own step |
| 0x8DF570 | 0x4DF570 | `FFX_MesWin_TypewriterAdvance` | characters per step |
| 0x8DE8C0 | 0x4DE8C0 | `FFX_MesWin_TextPageBegin` | sets the per-page rate **from the language** |
| 0x8DE970 | 0x4DE970 | `FFX_MesWin_TextPageStep` | page done -> textObject+44 = 2, +46 = 2 or 3 |
| 0x8E02C0 | 0x4E02C0 | `FFX_MesWin_TextPageWaitStep` | the parked "waiting for the player" step |

ATEL side of the same layer:

| VA | RVA | name | what |
|---|---|---|---|
| 0x86BE90 | 0x46BE90 | `FFX_Atel_GetMessageWindow` | the only correct way to reach a record |
| 0x86C8B0 | 0x46C8B0 | `FFX_Atel_MesWinBlockingKind` | 0 nothing, 1 plain box, 2 choice box |
| 0x8640F0 | 0x4640F0 | `FFX_Atel_MesWinRequestClose` | record+20 = 3 and deactivate |
| 0x863B10 | 0x463B10 | `FFX_Atel_MesWinResetAll` | clears both banks |
| 0x863C90 | 0x463C90 | `FFX_Atel_MesWinResetBank` | clears one bank |
| 0x8702E0 | 0x4702E0 | `FFX_Atel_SetMesWinBufSel` | the bank setter |
| **0x871F30** | **0x471F30** | **`FFX_Atel_MesWinPullStateAndGatePlayer`** | **pulls state, and FREEZES THE PLAYER** |
| 0x872360 | 0x472360 | `FFX_Atel_GetTextSpeedScale` | returns a constant out of `g_ffxAtelTextSpeedScale` |
| 0x872530 | 0x472530 | `FFX_Atel_SetTextSpeedScale` | written only by ATEL script syscall Core 601 |

## 3. The choice syscalls

The ATEL Core library table is at **0xC50050** and runs 616 slots of 16 bytes, so
**slot(N) = 0xC50050 + 16*N**, laid out `[start, poll, resf, resi]`. Registered by
`FFX_Atel_Init` 0x86D6D0 as library 0.

| Core fn | start | poll | resi | what the script gets |
|---|---|---|---|---|
| 100 | - | - | 0x857870 | set the message id. writes record+8/+12 and the attribute into record+22 |
| 101 | - | - | 0x8580C0 | set window x, y and skin |
| 102 | - | - | 0x858360 | set record+32, the palette |
| 103 / 104 | - | - | 0x858850 / 0x858640 | set explicit w and h |
| **105** | - | - | **0x8589D0** | **set the choice initial cursor (record+30) and cancel cursor (record+31)** |
| **106** | - | - | **0x858B50** | **OPEN THE WINDOW. decides plain / choice / numeric from the attribute** |
| 107 | - | - | 0x859060 | close the window |
| 124 | 0x85B7C0 | 0x85B980 | 0x85BA80 | wait for a plain confirm, variant |
| **125** | **0x859710** | **0x859C50** | **0x85A6F0** | **WAIT FOR A CHOICE AND RETURN THE ANSWER** |
| 132 | 0x85ABD0 | 0x85AE90 | 0x85B6D0 | wait for a plain confirm |
| 143 | - | - | 0x85DE10 | non-blocking "is this window idle or already answered" |
| **251** | - | - | **0x858AC0** | **read the LIVE cursor index, or -1** |
| 271 | - | - | 0x85AFD0 | set numeric entry digit cursor, cancel cursor, default value |
| 272 | - | - | 0x85B260 | read the numeric entry value |
| 313 | 0x85F730 | 0x85F850 | 0x85F900 | **ask a number**, auto geometry |
| 314 | 0x85FB30 | 0x85FCD0 | 0x85FD40 | ask a number, explicit geometry |
| **315** | **0x8602A0** | **0x8603D0** | **0x860760** | **ask a choice**, auto geometry. also reached as func 615 |
| **316** | **0x855D70** | **0x855EC0** | **0x856030** | **ask a choice**, explicit geometry |
| 489 | 0x8590F0 | 0x8592C0 | 0x8595F0 | close the window and wait until it is gone |
| 601 | - | - | 0x858FC0 (resf) | set the text speed scale |

Shared composite helpers, not bound to a slot of their own:

| VA | RVA | name | what |
|---|---|---|---|
| 0x8607A0 | 0x4607A0 | `FFX_AtelOp_AskChoiceComposite` | Core 315 and 316 body. attribute forced to `attr \| 2` |
| 0x860B00 | 0x460B00 | `FFX_AtelOp_AskNumberComposite` | Core 313 and 314 body. attribute forced to `attr \| 0x42` |
| 0x860990 | 0x460990 | `FFX_AtelOp_MesWinWaitChoiceDispatch` | a Core 125 poll variant with a per-option callback table |

### 3.1 The composite is the one to intercept

`FFX_AtelOp_AskChoiceComposite` pops 7 words and then replays, inside one start handler:
Core 101 (position) -> record+32 -> Core 100 (message) -> record+24 -> Core 105 (cursor
defaults) -> **Core 106 with the attribute forced to `attr | 2`** -> Core 125 start.

So a script asks a question in **one opcode**, and the whole setup is inside one function. That is
the cheapest single place for a mod to see "a question is being asked" and to learn its window
index.

### 3.2 Where the answer lives, and where the script reads it

`FFX_AtelOp_MesWinWaitChoice_start` 0x859710 stores into the command's own scratch block:

    cmdstate+0  dword  the window index
    cmdstate+4  s16    THE ANSWER, -1 for none
    cmdstate+6  u16    how many extra words the start handler popped (0..2)
    cmdstate+8..       those extra words, pushed back by the resi handler

`FFX_AtelOp_MesWinWaitChoice_poll` 0x859C50 is the only place the answer is produced. It returns
1 (command finished) when the window state is 0 or 4. Otherwise, on state 2, it needs **three**
conditions and all of them must hold:

1. `FFX_MesWin_GetTextPhase(win) == 3` - the message has fully finished drawing
2. `FFX_MesWin_GetChoiceState(win) == 2` - the player has confirmed
3. `FFX_MesWin_GetChoicePickCount(win) > 0`

then `cmdstate+4 = pickArray[0]` and it asks the window to close.
`FFX_AtelOp_MesWinWaitChoice_resi` 0x85A6F0 pushes that s16 onto the script stack.

**The cmdstate block is at thread+44**, which is the ATEL thread's own storage. See section 5.

### 3.3 The option count comes out of the MESSAGE, not the script

Two independent sources, and they are expected to agree:

- `FFX_Atel_GetMessageAttr` 0x86BF10 reads the u16 at `messageTable + 8*id + 2`. Its **high byte
  is the option count** and its low nibble is the digit count. `sub_871210` 0x871210, called from
  Core 106, clamps record+30 and record+31 against that count and **clears bit 0x02 when the
  count is zero**, so a choice attribute on a message with no options silently degrades to a
  plain box.
- `FFX_MesWin_LayoutMessageAndFindChoices` 0x8B9A60 walks the message token stream
  (`sub_8DF610` classifies a token, `sub_8DFD80` advances). Token 16 with operand 0x50..0x5F
  opens a choice sub-list and calls `FFX_MesWin_ChoiceGridSetup`. Token 16 with any other
  non-0xFF operand declares one selectable line and bumps choiceObject+5.

The message table itself is the **localised** `event/obj_ps3/<lang>/<name>.bin`, per
`FFX_Atel_GetMessageText` 0x86BF30. So the option count is a property of the installed language
data.

## 4. THE TWO INPUT PATHS. This is the most important finding in this document

A plain text box and a choice box take their input from **completely different places**.

### 4.1 A plain box goes through the ATEL pad

Inside `FFX_Atel_StepFrame` **0x867950**, at the top of the frame:

    ctx[0] &= ~4;                                   // clear last frame's routed confirm
    v7 = FFX_Pad__remapButtonMask(0, 0, g_ffxAtelPadPressed);   // contexts 0 and 1 only
    if ( (v7 & 0xA0) != 0 ) {
        kind = FFX_Atel_MesWinBlockingKind();
        if ( kind != 0 ) {
            if ( kind == 1 && (v7 & 0x20) ) ctx[0] |= 4;        // PLAIN box: route the press
        } else { arm the examine scan }                          // nothing open
    }

`FFX_AtelOp_MesWinWaitConfirm132_poll` 0x85AE90 then requires window state 2, text phase 3 **and
`ctx[0] & 0x04`**, and clears that bit as it closes the window.

So the plain box acknowledge is: `g_ffxAtelPadPressed` bit 0x20 -> `ctx+0` bit 0x04 -> Core 132
poll. Note `kind == 1` only. **For a choice box this code does nothing at all and the press is
dropped here.**

Separately, moving from page to page inside a multi-page message goes through
`FFX_MesWin_OnConfirmPressed` 0x8B64A0, which reads the MENU pad (below) and calls
`FFX_MesWin_AdvanceText`. So a plain box actually has two consumers of a confirm press: the page
advance on the menu pad, and the script acknowledge on the ATEL pad.

### 4.2 A choice box goes through the MENU input system

`FFX_MesWin_ChoiceListInput` 0x8B64E0 reads exactly two globals and nothing else:

| VA | name | what the choice handler does with it |
|---|---|---|
| 0x25D09D6 | `g_ffxMenuPadWord10` | direction with auto-repeat. 0x1000 left and 0x8000 up decrement the cursor, 0x2000 down and 0x4000 right increment it |
| 0x25D09D2 | `g_ffxMenuPadHeldTrig` | low word held, high word newly pressed. **0x200000 = CONFIRM** (records the cursor into the pick array), **0x400000 = CANCEL** (snaps the cursor to choiceObject+3) |

Both are written by `FFX_MesWin_SamplePadPort0` 0x8B7CD0, **port 0 hardcoded**, which also writes
`g_ffxMenuPadSynthHeldTrig` 0x25D09E2 and `g_ffxMenuPadRepeat` 0x25D09E6. The same four globals
are written by `FFX_MenuSys_SamplePad` 0x8BE500 and read by `FFX_MenuSys_GetHeld` /
`GetPressed` / `GetRepeat`, so the choice box and the in-game menu literally share one input
state.

**Verdict: a choice box does not use the ATEL pad at all. It uses the menu system's pad.** For
co-op that means two separate injection points, not one.

### 4.3 The gate on the choice input ever running

`FFX_MesWin_StepAll` 0x8AB910 runs the input handler only for the window whose
`g_ffxMesWinObjects[i] + 6 == 1`, that is, the one holding the **single global input focus**. And
`FFX_MesWin_RunConfirmHandler` 0x8B6B70 runs it only while `textObject+44 == 2`, the "page fully
drawn, waiting for the player" sub-state.

Frame order inside `FFX_MainStep` **0x820AE0**:

    FFX_Pad__updateAll()  ->  FFX_Pad__commitAllPorts()  ->  FFX_Player__readPad()
      ->  FFX_Atel_StepOnce()      // includes FFX_Atel_RunScript and so the Core 125 poll
      ->  FFX_MesWin_StepAll()     // samples the menu pad, then walks the 8 windows
      ...
      ->  if (g_ffxIsCatchUpStep) FFX_MesWin_ArmPadSampler(1); else FFX_MesWin_DrawAllWindows();

The script's poll runs BEFORE the player's confirm is processed, so an answer is always visible to
the script on the **next** step. That one-step lag is uniform, not a hazard.

**Injection point for a remote confirm:** after `FFX_MesWin_SamplePadPort0` returns and before the
8-window loop, OR the remote player's bits into `g_ffxMenuPadHeldTrig`'s high word
(0x200000 confirm, 0x400000 cancel) and into `g_ffxMenuPadWord10` for direction. In practice that
means hooking `FFX_MesWin_StepAll` or the tail of `FFX_MesWin_SamplePadPort0`.

## 5. Does the script block? Yes, as a proper yield

`FFX_Atel_RunScript` 0x8641E0 opcode **0x35 / 0x58** (call system function):

    if (thread[30] == 0) { thread[30] = 1; FFX_Atel_SysFuncStart(op, actor, thread+44, stack); }
    status = FFX_Atel_SysFuncPoll(op, actor, thread+44);
    if (status & 1) { FFX_Atel_SysFuncResult(...); thread[30] = 0; advance the PC; }

- `thread+30` u8 is the syscall phase, 0 not started, 1 started and polling
- `thread+44` is the command scratch block, which is what `a2` is in every start/poll/resi
- `thread+24` is the program counter and it is **only advanced when poll returns bit 1**

So the thread parks on the syscall opcode and the frame loop returns. **This is a stored resume
point, not a spin.** `FFX_Atel_SysFuncPoll` 0x877730 returns 5 for an unimplemented slot, and
the status bits are 1 = finished, 2 = keep blocking, 4 = yield.

A blocked script costs one actor thread on one channel and nothing global.
`FFX_Atel_StartThreadIfChannelFree` already refuses to start a second script on the same channel
of the same actor, so a conversation cannot be stacked on itself.

### 5.1 The player freeze, and it is not selective

`FFX_Atel_MesWinPullStateAndGatePlayer` **0x871F30** (RVA 0x471F30) is called from the tail of
`FFX_Atel_StepContextRange` 0x8688F0, **exactly once per range step**, against whichever context
was selected on entry, gated on `ctx+26 == 0`. It does:

1. for all 8 windows, `record+20 = FFX_MesWin_GetFrameState(i)`, with state 4 collapsed to 0
2. `anyChoice = 1` if any window is non-idle and has record+29 **bit 0x02** set
3. that goes into **`ctx+1` bit 0x04**
4. `FFX_Atel_UnbindPlayerChr()` **unconditionally** - walks every actor, zeroes the move speed of
   every controllable CHR, and calls `FFX_Ch_SetPlayerChr(0)`
5. `FFX_Atel_BindPlayerChr()` only when `ctx+1` bit 0x04 is clear, so a choice box leaves the
   player unbound and therefore frozen
6. `FFX_MesWin_SetInputFocus` on the **highest-indexed** window in state 2 with record+29 bits
   0x32

**So one player opening a choice box stops both characters moving.** Note that a plain text box
(bit 0x10 with no 0x02) does NOT set the flag, so plain dialogue does not freeze movement through
this path. Scripted conversations freeze the player some other way, presumably a control-disable
syscall, which is not covered here.

`FFX_Atel_UnbindPlayerChr` is 0x8719E0, `FFX_Atel_BindPlayerChr` is 0x871AB0.

## 6. One per WHAT, for every piece of state

This is the question that matters. Answered for each slot found.

| state | VA | one per WHAT | breaks a second player? |
|---|---|---|---|
| ATEL window record (44 bytes) | 0x1326D70 | **(bank, window index 0..7)**. NOT per ATEL context, all 7 contexts share the same 8 | only if two scripts pick the same window index |
| record state (+20) | per record | same | same |
| record attribute bits (+29) | per record | same | same |
| record cursor defaults (+30/+31) | per record | same | same |
| window shell | 0x1865B3C | (bank, window index) | same |
| **input focus (shell+6)** | 0x1865B3C | **ONE GLOBAL. exactly one of the 8 windows at a time** | **YES, this is the real one** |
| text object | 0x18676F0 | (bank, window index) | only on index collision |
| text sub-state (+44) and phase (+46) | per text object | same | same |
| **choice object** | 0x1868A90 | **(bank, window index)** | only on index collision |
| **cursor index (choice+2)** | per choice object | **(bank, window index)** | only on index collision |
| **choice state (choice+1)** | per choice object | same | same |
| **pick array (choice+24)** | per choice object | same | same |
| numeric value (choice+12) | per choice object | same | same |
| frame object and lifecycle (+8) | 0x1876DA8 | (bank, window index) | same |
| **the ANSWER (cmdstate+4)** | thread+44 | **ONE PER ATEL THREAD** | **no. this is the one genuinely per-conversation slot** |
| bank selector | 0x1326B82 | ONE GLOBAL, field vs menu | no, it is a mode not a player |
| menu pad held/pressed | 0x25D09D2 | ONE GLOBAL, port 0 only, shared with the menu | **YES** |
| menu pad direction+repeat | 0x25D09D6 | ONE GLOBAL, port 0 only | **YES** |
| menu pad hold counts | 0x25D09C2 | ONE PER BUTTON BIT (16), one set globally | yes, for auto-repeat only |
| menu pad hold timers | 0x25D0A00 | same | yes, and see section 7 |
| pad sampler arm | 0x25D09C0 | ONE GLOBAL | no, but see section 7 |
| confirm one-shot latch | 0xC5A03C | ONE GLOBAL across all 8 windows | yes if two boxes ever both wait |
| text speed scale | 0x1325B98 | ONE PAIR GLOBALLY, script-written only | no |
| font mode | 0x1865F00 | ONE GLOBAL, derived from the language | only if the two machines run different languages |
| player freeze flag | ctx+1 bit 0x04 | per ATEL context, decided from the SHARED 8-window scan | **YES, one player's box freezes everybody** |
| plain-box routed confirm | ctx+0 bit 0x04 | per ATEL context, written from the one ATEL pad mask | yes |

### 6.1 Two simultaneous conversations, concretely

The choice state is per window slot, which is better than one global. But the **input focus is a
single slot**, and `FFX_Atel_MesWinPullStateAndGatePlayer` re-asserts it **every frame** onto the
highest-indexed waiting window. So if two scripts open choice boxes on windows 0 and 3:

- window 3 owns the pad forever
- window 0's choice object never advances past state 0 or 1
- window 0's script sits in its Core 125 poll indefinitely

That is deterministic and identical on both machines, so it is not a desync. It is a **hang**.
Which window index a script uses is script data, so this cannot be fixed by picking indices, only
by arbitrating focus.

## 7. Non-determinism in this layer

I looked specifically for: wall-clock reads that change a branch, RNG, and dependence on
rendering. Scoped by xref from the clock and RNG functions into the ATEL syscall range
0x855000-0x878000, the window range 0x8AB000-0x8BA000, the text engine 0x8DE000-0x8E0200 and the
frame layer 0x906B00-0x908000. Result:

**One wall-clock read in the whole layer.** `FFX_MesWin_SamplePadPort0` 0x8B7CD0 calls
`FFX_Input__getTimeSeconds` 0x630C60 to drive the auto-repeat timers at
`g_ffxMenuPadHoldTimer` 0x25D0A00, with a 0.2333 s first delay and 0.1333 s after. So:

- a **single confirm or cancel press** is a pure edge test on the mask and is deterministic
- a **held direction** generating repeats is NOT deterministic, the repeat count depends on real
  elapsed time

Consequence: replicating the raw pad mask is not enough for a held direction. The cursor index
would drift between machines. **Replicate the resulting cursor index, or replicate discrete
cursor-move events, not held buttons.**

**No RNG anywhere in the message or choice path.** The only `FFX_Rand_Stream` callers in the ATEL
syscall range are Core 166 (0x857400, bounded random) and Core 169 (0x857680, raw 16 bit), both
stream 2. Those are general script RNG, not dialogue, and they are already a known lockstep
concern.

**The typewriter has no clock and no frame counter.** `FFX_MesWin_TypewriterAdvance` 0x8DF570 does
`textObject+176 += FFX_Atel_GetTextSpeedScale(0) * textObject+172`, then integer-divides by 30
(or 25) to add whole characters. `FFX_Atel_GetTextSpeedScale` returns a constant, 2.0 from
`FFX_Atel_Init`, written only by ATEL script syscall Core 601, so it is script state and
replicates with the script. It is driven once per simulation step out of `FFX_MainStep`.

**But the language changes how many steps the text takes, in three places:**

1. **Language.** `FFX_MesWin_TextPageBegin` 0x8DE8C0 sets `textObject+172` to **30 for languages
   0, 1, 16, 17, 18 and 25 for languages 2 to 6**, out of `j_FFX_GetLanguage()`. Different
   language, different number of steps to finish a page, therefore a different step on which the
   answer becomes readable.
2. **`g_ffxMesWinFontMode` 0x1865F00.** Picks the 25-vs-30 divisor in the typewriter and the
   glyph advance (0.73 vs 0.65) in `FFX_MesWin_MeasureTokenRun`. Its only writer is `sub_8AD900`
   0x8AD900, and that function **derives it from the language**: 1 for 2 fr, 3 sp, 4 de, 5 it and
   18, 0 for 0 jp and 1 en. So it is not an independent graphics option, it rides on the language.
3. **The option count itself** comes out of the localised message table, so a language with a
   different option count clamps the default cursor differently.

**There is a dependence on the draw pass.** `FFX_MesWin_DrawChoiceCursorAndArm` 0x8B5FB0 is the
only thing that flips choiceObject+1 from 0 to 1, and its only caller is
`FFX_MesWin_DrawWindow` 0x8B5DC0, reached from `FFX_MesWin_DrawAllWindows` 0x8ABD30. That draw
pass is inside `FFX_MainStep`, not on a render thread, which is good. But `FFX_MainStep` does:

    if (g_ffxIsCatchUpStep) FFX_MesWin_ArmPadSampler(1); else FFX_MesWin_DrawAllWindows();

so on a **catch-up step the draw is skipped**. The pad sampler is still armed, and
`FFX_MesWin_ChoiceListInput` writes state 2 directly when the pick completes, so the answer path
still works. What does not happen on a catch-up step is the 0 -> 1 arm, so **ATEL Core 251, the
live cursor read, returns -1 on those steps**.

`FFX_StepPacing_AllowCatchUp` 0x81FD30 only blocks catch-up for a dialogue box in one specific
scene (scene id 6991 with blocking kind 2), and it returns 1 unconditionally when
`FFX_Booster_IsSpeedUpActive` is set, which is a **local user toggle**. So whether a given step
drew the windows is not guaranteed to match between two machines.

## 8. Verdict: what a co-op mod has to do

In order of how much it buys.

1. **Replicate the ANSWER, not the input.** The answer is one s16 at `cmdstate+4`, produced in
   one function (`FFX_AtelOp_MesWinWaitChoice_poll` 0x859C50) by three reads. That is the
   narrowest possible waist. The cheapest correct design is: the owning player's machine decides
   the answer, the answer goes on the wire as `(windowIndex, pickIndex)`, and both machines write
   `choiceObject[24] = pickIndex`, `choiceObject[7] = 1`, `choiceObject[1] = 2` on the agreed
   step. Both polls then latch the identical value. This sidesteps the auto-repeat clock, the
   language-dependent text speed and the catch-up draw skip in one move.
2. **If you replicate input instead, replicate cursor DELTAS and the confirm EDGE, never held
   buttons.** The auto-repeat timer in `FFX_MesWin_SamplePadPort0` is the only wall-clock read in
   the layer and it is in exactly the path a held direction takes.
3. **Arbitrate the input focus.** `FFX_MesWin_SetInputFocus` 0x8AF7D0 is one global slot for 8
   windows, and `FFX_Atel_MesWinPullStateAndGatePlayer` re-asserts it every frame onto the
   highest-indexed waiting window. Two simultaneous conversations means one of them hangs. Either
   forbid simultaneous conversations, or hook the focus re-assert and give each player's
   conversation a focus turn.
4. **Decide what the freeze should mean.** A choice box unbinds the player CHR for everybody
   through `ctx+1` bit 0x04, and `FFX_Atel_UnbindPlayerChr` 0x8719E0 zeroes every controllable
   CHR's move speed. If one player talking should not stop the other, that bit and that function
   are the two places to intervene, and the intervention has to be identical on both machines.
5. **Lock the language in the lobby.** It sets the typewriter rate directly
   (`textObject+172`, 30 or 25) and it also sets `g_ffxMesWinFontMode`, which picks the divisor
   and the glyph advance. Both change how many simulation steps a page of text takes, and
   therefore the step on which a choice becomes answerable. The option count also comes out of
   the localised message file. Lock the booster speed-up toggle too, since it changes whether the
   window draw pass runs on a given step.
6. **Gate interaction on `FFX_Atel_MesWinBlockingKind` on BOTH machines, not just the initiator.**
   It reads the shared 8-record array, so both machines already compute the same answer, but a
   co-op command executor that fires an examine event while a box is open will do something the
   engine itself refuses to do (see the gate in `FFX_Atel_StepFrame`).
7. **Intercept at the composite, not at Core 125.** `FFX_AtelOp_AskChoiceComposite` 0x8607A0 and
   `FFX_AtelOp_AskNumberComposite` 0x860B00 are single functions that do the whole setup, so they
   are the one place a mod can learn "a question is being asked on window N with M options"
   before the player can touch it.

Numbers worth having to hand, because they make several of these trivial:
- picks required is **hardwired to 1** by Core 106, so every choice yields exactly one byte
- the answer is **one s16** per conversation, living per ATEL thread
- the pad injection is **two bits** in one dword plus four direction bits in one word

## 9. What is still unsettled

- **Which window index ordinary NPC conversations use.** Every message syscall takes it as the
  first argument and it comes out of the script, so it can only be answered by reading scripts or
  by watching the game. If every script uses window 0, then the per-window-slot isolation found
  in section 6 buys nothing and the focus problem in section 6.1 is immediate.
- **Who writes `byte_25D09C1`** (the second half of the pad sampler arm at 0x25D09C0). Nothing in
  the binary writes it except the 0xC0-byte memset in `FFX_MenuSys_ClearPad` 0x8BE3D0, and the
  static image has 0xFF there. So either the memset permanently disables it and the arm then
  depends entirely on `g_ffxMenuPadBlock`, or there is a writer I did not find. The practical
  conclusion (the sampler runs every step because the draw pass or
  `FFX_MesWin_ArmPadSampler(1)` arms it) holds either way, but the mechanism is not fully pinned.
- **Whether any shipped script reads the LIVE cursor** through ATEL Core 251 0x858AC0 and branches
  on it. The opcode exists and it would be a problem for an answer-only replication design,
  because the live cursor is -1 on catch-up steps. No such script is known.
- **Whether `thread+44` is big enough for anything the mod might want to stash there.** I read
  16 bytes of use by Core 125 and did not establish the allocated size in `FFX_Atel_CreateThread`
  0x86EA00.
- **How a plain scripted conversation freezes the player.** Section 5.1 proves that only a CHOICE
  box sets the freeze flag. Plain dialogue clearly also stops you in game, so there is another
  mechanism, probably an ATEL control-disable syscall, that is outside this document.
- **`sub_8AD900`, `sub_8AC390`, `sub_7851B0`, `dword_25D16BC`** all appear in the geometry and
  text-source path and look like display or voice-mode flags. Not traced. None of them feed the
  answer, but several feed layout, and layout feeds the option rectangles.
- **UNTESTED IN A RUNNING GAME.** Everything above is static reading. The three-condition answer
  gate in section 3.2 and the focus hang in section 6.1 are the two claims most worth confirming
  live before building on them.
