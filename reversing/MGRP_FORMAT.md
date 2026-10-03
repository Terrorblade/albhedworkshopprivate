# The `.mgrp` motion group format

3,156 of them in `FFX_Data.vbf`, the most common non-Phyre extension. Reader: `../tools/mgrp.py`,
which parses all 3,156 with **zero structural rejections**.

An `.mgrp` is a relocatable bundle of motion banks. The game loads one whole and fixes its internal
offsets up into pointers in place, so every offset below is a plain file offset as stored.
`FFX_Mot_RelocateBundle 0x836F70` computes `delta = &hdr - hdr[0]`, stamps `hdr[0] = &hdr` (which
makes it idempotent) and adds `delta` to every pointer underneath. **`dword[0]` is 0 in all 3,156
files**, so the baked pointers are file-relative.

## Where they live

```
ffx_ps2/ffx/master/jppc/chr/<cat>/<name>/mot/resident{0,1,2,3}.mgrp    per-character motion sets
ffx_ps2/ffx/master/jppc/event/obj/<xx>/<event>/<event><NN>.mgrp        per-event motion groups
```

The naming is pinned by the debug tag `'[%s:resident%d.mgrp]'` at `0xB59700` in
`FFX_Ch_RomReadMotionSet 0x82A4C0`. The four modes are **0 Field, 1 FieldBattle, 2 Swim,
3 SwimBattle**, matching `g_ffxDebugCharModeNames 0xC34318`.

For events, `NN` is the motion group index and the archive slot is `eventBase*18 + N + 3`, so up to
15 groups per event. 11 is the most observed.

2,371 of the 3,156 are empty 16-byte placeholders. The real ones hold 4,406 banks, 22,125 motions
and 21,165 clips in total.

## Layout

### Bundle header, 16 bytes

| off | type | field | status |
|---|---|---|---|
| `0x00` | u32 | relocation anchor, 0 on disk | confirmed |
| `0x04` | u32 | bank count | confirmed |
| `0x08` | u32 | unused, 0 in all 3,156 | confirmed unread |
| `0x0C` | u32 | offset of the bank descriptor array, always at the file tail | confirmed |

### Group bank descriptor, 20 bytes

From `FFX_Mot_RelocateGroupBank 0x836FC0` and `FFX_Mot_RegisterGroup 0x836D30`.

| off | type | field | status |
|---|---|---|---|
| `0x00` | u32 | 0 in 4,142 of 4,406 banks, otherwise a `0x40xxxxxx` leftover PS2 EE address | inferred, never read |
| `0x04` | u16 | model id (1 = `c001`) | confirmed |
| `0x06` | u16 | 0 in all 4,406 | confirmed unread |
| `0x08` | u16 | motion table entry count | confirmed |
| `0x0A` | u16 | clip table entry count | confirmed |
| `0x0C` | u32 | motion table, 16-byte stride | confirmed |
| `0x10` | u32 | clip table, 16-byte stride, the base PLAY indexes | confirmed |

### Motion table entry, 16 bytes

| off | type | field | status |
|---|---|---|---|
| `0x00` | u16 | motion index, matched against `LOWORD(motionId)` | confirmed |
| `0x02` | u16 | model id, always equals the bank's in 22,125 of 22,125 | confirmed by data |
| `0x04` | u16 | label count | inferred, exact for all 22,125 (`p12 - p8 == 2*n`) |
| `0x06` | u16 | sequence bytecode byte length | inferred, exact for 22,109 of 22,125 and over by 7-10 on 16 entries. Never read by the exe |
| `0x08` | u32 | `u16` label offset table, the JUMP targets | confirmed |
| `0x0C` | u32 | sequence bytecode, becomes `CHR+0x714` | confirmed |

A **motion id is `(modelId << 16) | motionIndex`**.

### Clip table entry, 16 bytes

| off | type | field | status |
|---|---|---|---|
| `0x00` / `0x02` / `0x06` | u16 | 0 in all 21,165 | confirmed unread |
| `0x04` | u16 | frame marker count | inferred, `(p12-p8) >= 2*n` for all 21,165 |
| `0x08` | u32 | `s16` frame marker array | confirmed |
| `0x0C` | u32 | animation clip data | confirmed |

### Animation clip header, 20 bytes

All confirmed, from `FFX_Mot_BindChannels 0x839980` and `FFX_Mot_StartClip 0x839B80`.

| off | type | field |
|---|---|---|
| `0x00` | u16 | total frames |
| `0x02` | u16 | joint count (136 for `c001`) |
| `0x04` | u8 | 0 everywhere, unknown |
| `0x05` | u8 | frame rate, 30. The u16 at `+4` becomes `m_frameRateScale` = `0x1E00` = `30 << 8` |
| `0x06` | u16 | motion event count |
| `0x08` | u32 | relative offset of the 2-bit-per-channel mode bitstream |
| `0x0C` | u32 | relative offset of the key and value stream |
| `0x10` | u32 | relative offset of the event array |

### Motion event record

From `FFX_Mot_RunEvents 0x834350`, stride `8 + 4*n`: byte 0 is the type (**1 SND, 2 EFF, 3 TEX,
5 BTL**), byte 1 the payload dword count `n`, then `u16` start frame, `u16` end frame, `u16` id.

These are how walk and run were told apart independently of slot order. For `c001`, the run clip
fires SND id 1 at frames 6 and 18 (a 12-frame footstep cadence) and the walk clip fires SND id 2 at
frames 8 and 22 (14 frames).

## The sequence bytecode

Pinned from the disassembly at `0x837946-0x8379C6` in `FFX_Mot_SeqExec 0x837900`.

| op | name | size | operands |
|---|---|---|---|
| 0 | `END` | 1 | - |
| 1 | `PLAY` | 9 | u16 clipIdx, u16 loopCount (**0 = forever**), u16 startMarkerIdx, u16 endMarkerIdx |
| 2 | `WAIT_CLIP_END` | 1 | blocks while `m_clipPlaying != 0` |
| 3 | `WAIT_FRAMES` | 3 | u16 n |
| 4 | `JUMP` | 3 | u16 labelIdx -> `entry[0x0C] + (s16)labelTable[idx]` |
| 5 | `STOP` | 1 | - |
| 6 | `WAIT_KEY` | 3 | blocks while `(int8)CHR+0x737 < n` |

`PLAY` resolves `start = markers[startIdx]` and `end = markers[endIdx]`, then
`if (endIdx != 1 && loop != 1) ++end`.

Opcode census over all 3,156 files: `PLAY` 32,029, `END` 22,125, `WAIT_CLIP_END` 6,692, `WAIT_KEY`
3,269, `WAIT_FRAMES` 3, and **`JUMP` 0, `STOP` 0**. Both of the last two are implemented but unused
by shipped data, so an assembler could emit them but nothing proves the game handles them correctly
in practice.

`WAIT_KEY`'s gate at `CHR+0x737` is read **signed**: `movsx cx, byte ptr [ebx+737h]` then `cmp cx,
ax` / `jl` at `0x8379FD`. So the 255 that `FFX_Ch_SetMotionKey` seeds reads back as -1 and blocks the
opcode even at threshold 0. Only the event script opcodes at `0xA78710` and `0xA79550` raise it, via
`FFX_Ch_SetSeqWaitKey 0x837570`.

## Registration and playback

```
FFX_Ch_RomReadMotionSet       0x82A4C0   async read, chcache type 2, key = modelId | (mode << 16)
FFX_Ch_MotionSetReadStart     0x836A40 / ReadSync 0x836A50
FFX_Ch_LoadMotionSetSync      0x836870   idempotent, no-ops if FFX_Mot_FindBundle already hits
  FFX_Mot_RegisterBundle      0x836C90   slot in g_ffxMotBundleTable 0x13011E0 (50 x 12 bytes)
    FFX_Mot_RelocateBundle    0x836F70
      FFX_Mot_RelocateGroupBank 0x836FC0
    FFX_Mot_RegisterGroup     0x836D30   per bank -> g_ffxMotGroupTable 0x1300A08 (100 x 20)
      FFX_Mot_RebuildGroupChains 0x838170 -> g_ffxMotGroupChainHead 0x13011D8

FFX_Ch_SetMotionKey           0x837AC0   motionId -> the CHR's motion pointers. Does NOT start it
FFX_Mot_SetByModeIndex        0x837D00   logical index -> motionId via the .chr table

FFX_MainStep 0x820AE0 at 0x82107D
 -> FFX_Ch_UpdateMotionAll 0x832E10
     pass 1: per CHR, if !(m_flags1 & 0x40): locomotionMode 0 -> FFX_Ch_AutoLocomotionAnim 0x835BB0
                                             locomotionMode 2 -> FFX_Ch_AutoSwimAnim 0x835D30
     FFX_Ch_ResolveCollisionsAll 0x83D3B0, then the ground and water clamp
     pass 3 at 0x833273: per CHR FFX_Mot_AdvanceFrame 0x838C90
        FFX_Mot_StepSequence  0x8377C0 -> FFX_Mot_SeqExec 0x837900
           -> FFX_Mot_StartClip 0x839B80 -> FFX_Mot_BindChannels 0x839980
        FFX_Mot_EvalChannels  0x8393C0 -> FFX_Mot_DecodeKeyStream 0x8394D0
        FFX_Mot_BlendChannels 0x8395B0
        FFX_Mot_BuildJointTransforms 0x838F70 -> FFX_Mot_ComposeJointSrt 0x838280
        FFX_Mot_WrapFrameAtEnd 0x8392D0
 -> FFX_Ch_DispatchInBatches 0x833390 -> FFX_Mot_RunEvents 0x834350
render bridge: FFX_Ch_BuildSkinMatrices 0x832760 -> FFX_ChrInstance_UploadBoneMatrices 0x63BE60
```

The bundle registry key is the triple `(kind, id, modelId)` where kind 0 is a character resident set
(id = mode), kind 1 an event motion group (id = group number) and kind 2 the system mgrp.

## Two CHRs can share one motion bundle safely

See the audit section in `PHASE1_CLONE.md`. Short version: every function in the playback path writes
zero globals, the decode cursors live in the CHR's own per-joint channel array, and there is no back
pointer from bundle, motion entry or clip to a CHR. The only in-place mutation of bundle data is
`FFX_Mot_RelocateBundle`, which is self-guarded.

One thing to know: `m_motionBundle` (`CHR+0x7E8`) records the owning bundle, and
`FFX_Ch_ReleaseResource 0x836E20` walks the **whole** CHR array clearing the motion pointers on every
CHR using that bundle. So unloading a motion set stops a clone too. The matching
`FFX_Ch_RelocateResourceBlock 0x8370C0` also walks all CHRs, which works in our favour.

## Known unknowns

- **Bank `+0x00`**, a `0x40xxxxxx` value in 264 of 4,406 banks and never read. Searching an original
  PS2 ELF for `0x40104440` would say whether it is a stale EE pointer.
- **Motion entry `+0x04` and `+0x06`** are label count and bytecode length by exact arithmetic, but
  no code reads them, and 16 entries over-report the length by 7-10. An original exporter would
  settle it.
- **Clip entry `+0x00`/`+0x02`/`+0x06` and clip header `+0x04`** are 0 in 100% of shipped data. Only
  a non-retail `.mgrp` would reveal them.
- **Motion index semantics.** They cluster (`0x104x` idle-ish, `0x109x` locomotion, `0x12xx` battle,
  `0x13xx` event) but there is no shipped name table. `FFX_Mot_GetName 0x8371F0` reads
  `<dataPath>/inf/motion.tbl` (64-byte records, u16 index at +0, name at +32) but it is debug-gated
  and `motion.tbl` is not in the archive. A `motion.tbl` from a debug build would decode them all.
- **Duplicate motion indices inside one bank**, e.g. `0x10D4` three times and `0x1232` eight times in
  `c001/resident3`. `FFX_Ch_SetMotionKey` takes the first match, so the later ones look unreachable.
  Diffing their bytecode would settle whether they differ at all.
