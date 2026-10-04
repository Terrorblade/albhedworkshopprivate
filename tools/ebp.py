#!/usr/bin/env python3
"""
ebp.py - parser and prototype disassembler for FINAL FANTASY X HD Remaster
          ATEL event script packages (.ebp / "EV01").

Reads straight out of FFX_Data.vbf via vbf.py, or from a loose file.

Everything printed here was checked against the binary (FFX.exe) and against all
397 .ebp files in the archive. Anything still unknown is printed as raw bytes or
labelled "unk", never guessed.

Usage
  python ebp.py list                          list the .ebp files in the archive
  python ebp.py info   <name-or-path>         container + ATEL block map
  python ebp.py actors <name-or-path>         actor definition table
  python ebp.py vars   <name-or-path>         variable descriptor table
  python ebp.py consts <name-or-path>         int / float constant pools
  python ebp.py text   <name-or-path>         message tables
  python ebp.py dis    <name-or-path> [-a N]  disassemble the bytecode
  python ebp.py verify                        re-run the structural checks on all files

<name-or-path> can be a full archive path, a bare event name like azit0000,
or a path to a file on disk.

=============================================================================
CONTAINER  (.ebp, 0x40-byte header, confirmed)
=============================================================================
  +0x00  char[4]  "EV01".  FFX.exe only compares the first two bytes ('E','V')
                   in FFX_Ev_LoadEventPackage 0x872EF0, and spins in while(1)
                   on a mismatch.  That is why the dword 0x31305645 appears
                   nowhere in .text.
  +0x04  u32[n]   section offsets from the start of the file, terminated by
                   0xFFFFFFFF.  The LAST entry before the terminator is the
                   end-of-data sentinel, not a section.
                   365 files have 6 entries (5 sections), 32 legacy/debug files
                   have 5 (4 sections).  The rest of the 0x40 header is zero in
                   all 397 files.
  Section roles, as read by FFX_Ev_LoadEventPackage:
    0 (hdr+0x04) ATEL script block.  Always 0x40.  -> dword_23CBD70
    1 (hdr+0x08) built-in Japanese message table (fallback when the per-language
                 event/obj_ps3/<xx>/<name>.bin is missing)
    2 (hdr+0x0C) SeSep sound-effect package -> FFX_Atel_StartEventSe 0x866430
    3 (hdr+0x10) built-in font block.  Tagged 'FTCX' in 324 of the 330 files
                 that have it; the other 6 (mmmc0000, sample01, sample02,
                 test15, test39, lchb0806) hold an older untagged variant:
                 u32 glyph count then per-glyph width bytes.  Never read by
                 the PC build - it always takes the localized .ftc instead.
    4 (hdr+0x14) built-in US/default message table.  Same codec and record
                 layout as the shipped new_uspc/.../<name>.bin; byte-identical
                 in 81 of the 294 files that have both, an older revision of the
                 same strings in the other 213.  Section 1 is the same thing for
                 Japanese (9 byte-identical to new_jppc).
    5 (hdr+0x18) end of data (== file size for the 365 six-entry files)

=============================================================================
ATEL SCRIPT BLOCK  (offsets from the block base, which is file+0x40)
=============================================================================
  +0x00  u32  unknown, nonzero in every file                            [unk]
  +0x04  u32  -> map entry / warp record table, 32 bytes per record.
              s16 at record+0 is the map number (FFX_Atel_GetEntryMapNo
              0x86BD90), then three u16 and four floats.
  +0x08  u32  -> short name string (an actor or group name)
  +0x0C  u32  -> event name string ("azit0000")
  +0x10  u32  total size of the ATEL block.  The per-actor entry and label tables
              are the last thing in the block; this value is their end padded up
              to 16 (the gap is 0, 4, 8 or 12 in all 397 files).  The next
              container section starts at this value rounded up to 0x40.
  +0x14  u16  count of 1464-byte actors
  +0x16  u16  count of 1368-byte actors
  +0x18  u16  unknown count                                             [unk]
  +0x1A  u16  unknown                                                   [unk]
  +0x1C  u16  count of 744-byte actors
  +0x1E  u16  unknown flags (0x8010 is common)                          [unk]
  +0x20  u32  -> base used by variable storage class 6
  +0x24  u32  -> unknown small block                                    [unk]
  +0x28  u32  -> table of 16 u16                                        [unk]
  +0x2C  u32  -> RES, the resource/boot sub-header (see below)
  +0x30  u32  -> code section base.  All script entry points and jump targets
              are byte offsets from here.
  +0x34  u16  total actor count = length of the offset array at +0x38
  +0x36  u16  number of "large" actors; actors [this, total) use the 48-byte
              pool stride (FFX_Atel_CalcActorPoolSize 0x86A290)
  +0x38  u32[+0x34]  offset of each actor definition record

  Character actors are 2904 bytes in the live pool, then 1368, 1464, 744 and 48
  byte groups in that order (FFX_Atel_GetActor 0x86A830).  Character count is
  u16[0x36] - u16[0x14] - u16[0x16] - u16[0x1C].

  Layout inside the block, in file order (azit0000, and the same shape in every
  file checked): fixed header, actor-offset array, actor records, map-entry
  table, name strings, variable descriptor table, int pool, float pool, CODE,
  resource list, RES sub-header, small tables, then per-actor entry and label
  tables up to +0x10.

ACTOR DEFINITION RECORD (52 bytes, confirmed)
  +0x00  u8   actor type.  Counts over all 397 files: 0 x441, 1 x6707, 2 x648,
              3 x486, 4 x1944, 5 x1382, 6 x116.
                0 subroutine container.  All 5560 opcode-0x33 callactor targets
                  are type 0, and all 441 opcode-0x34 ret instructions sit inside
                  a type-0 actor - exactly one ret per type-0 actor.
                1 character, 2 line trigger, 3 box trigger,
                4 no motion state, 5 and 6 unidentified
  +0x01..0x07 unknown                                                   [unk]
  +0x08  u16  number of script entry points (table at +0x20)
  +0x0A  u16  number of jump labels        (table at +0x24)
  +0x0C..0x13 unknown                                                   [unk]
  +0x14  u32  -> variable descriptor table (8 bytes per entry)
  +0x18  u32  -> int constant pool   (opcode 0x2D indexes it)
  +0x1C  u32  -> float constant pool (opcode 0x2F indexes it)
  +0x20  u32  -> script entry-point table, u32 code offsets.  Opcode 0x33 CALL
                 runs entry 0 of the named actor; sub_8691D0 resumes at entry 1.
  +0x24  u32  -> jump label table, u32 code offsets.  Opcodes 0x30/0x56/0x57
                 index it (sub_8726F0 0x8726F0).
  +0x28  u32  base for variable storage class 2
  +0x2C  u32  base for variable storage class 3
  +0x30  u32  base for variable storage class 4.  In practice this is also where
              the resource/CHR list lives (same value as RES+0x0C).

VARIABLE DESCRIPTOR (8 bytes; FFX_Atel_ResolveVarAddress 0x86C2E0)
  +0x00  u32  descriptor:
                bits 28..31  type  0 u8, 1 s8, 2 u16, 3 s16, 4 u32, 5 s32, 6 f32
                bits 25..27  storage class
                               0  *(ctx+0x2C) + off   (global / save RAM A)
                               1  *(ctx+0x30) + off   (global / save RAM B)
                               2  atel + off + actorDef[0x28]
                               3  ctx+0x54 callback, else atel+off+actorDef[0x2C]
                               4  atel + off + actorDef[0x30]
                               5  actor + 0x48 + off  (the register block)
                               6  atel + off + atel[0x20]
                bit  24      a separate flag, NOT part of the offset
                bits 0..23   byte offset within that class

              The offset is 24 bits, not 25. FFX_Atel_ResolveVarAddress masks the
              descriptor with 0xF0FFFFFF in all eight places it touches it, which
              clears bit 24 along with the class bits, and it reads the class with
              'shr eax,25; and eax,7'. With a 25-bit mask 1,476 of the 3,281
              class-0 descriptors across the 397 packages land outside the
              0x2000-byte ScriptWork region they index. With 24 bits all of them
              fit.
  +0x04  u16  element count (bounds check for the indexed forms)
  +0x06  u16  unknown                                                   [unk]

RES SUB-HEADER (atel + atel[0x2C]; partially decoded)
  +0x04  u32  spare CHR count            FFX_Atel_GetSpareChrCount 0x86AA20
  +0x08  u32  flags                      FFX_Atel_GetResFlags 0x86ADC0
  +0x0C  u32  -> resource/CHR list: u32 count, then {u32 id, u16 flags} on an
              8-byte stride starting at +4  FFX_Atel_LoadResourceList 0x8658D0
  +0x1C  u32  -> 16 u16 sound-group offsets   (slots 0/1/4/5)
  +0x20  u32  -> 16 u16 sound-group offsets   (slots 2/3/6/7)
  +0x2C  u32  -> block handed to sub_885FC0
  +0x30  u32  -> 16 u16 sound-group offsets   (slots 8/9/4/13)
  +0x34  u32  -> 16 u16 sound-group offsets   (slots 10/11/14/15)
  +0x38  s32  if negative, (u16) of it is the event BGM id
                                        FFX_Atel_LoadEventBgm 0x865A60
  +0x3C  u32  -> sub-table T; T+0x08 and T+0x0C are further offsets
  Other fields are not yet identified.

=============================================================================
BYTECODE  (FFX_Atel_RunScript 0x8641E0, fetch FFX_Atel_FetchInstruction 0x869D60)
=============================================================================
  b0 = code[pc]
  if b0 & 0x80:  opcode = b0 & 0x7F;  operand = code[pc+1] | code[pc+2]<<8; len 3
  else:          opcode = b0;         no operand;                          len 1
  The interpreter handles opcodes 0x00..0x7A.  Operand-carrying opcodes are the
  same numbers with 0x80 set in the byte.  0x1B and 0x41..0x44 are inside that
  range but have no case in the switch, so they fall to the error default;
  anything above 0x7A is rejected the same way.  "a" below is the operand pushed
  first, "b" the one on top of the stack.

  Operand stack: 19 slots at actor+0xC4 (sp) / actor+0xC8 (values) with a
  parallel type-tag byte array at actor+0x118 (1 = int, 2 = float).
  Per-channel script thread: actor+0x12C + 76*channel, 9 channels.
    +0x00..0x08 saved return code offsets (call depth 3)
    +0x10..0x14 saved caller actor ids
    +0x18       PC (host pointer)
    +0x1D       call depth
    +0x1E       system-function state (0 idle, 1 running)
    +0x1F       waiting-on-another-actor state
    +0x20       condition register (the conditional jumps write it)
    +0x24       second scratch register
    +0x28       float result register R
    +0x2C..     system-function argument block / wait record
  Registers: 4 ints at actor+0x48..0x54, 10 floats at actor+0x58..0x7C.
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

DEFAULT_VBF = (r"G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
               r"\data\FFX_Data.vbf")
EBP_DIR = "ffx_ps2/ffx/master/jppc/event/obj"

SECTION_NAMES = [
    "atel",       # 0  hdr+0x04
    "msg_jp",     # 1  hdr+0x08
    "se_sesep",   # 2  hdr+0x0C
    "ftcx",       # 3  hdr+0x10
    "msg_us",     # 4  hdr+0x14
]

# ---------------------------------------------------------------- archive -----


class Source(object):
    """Reads .ebp bytes either from the VBF or from disk."""

    def __init__(self, vbf_path=DEFAULT_VBF):
        self._arc = None
        self._vbf_path = vbf_path

    @property
    def arc(self):
        if self._arc is None:
            import vbf
            self._arc = vbf.VbfArchive(self._vbf_path)
        return self._arc

    def list_ebp(self):
        return sorted(e.name for e in self.arc.entries if e.name.endswith(".ebp"))

    def resolve(self, ref):
        if os.path.isfile(ref):
            return ref
        names = self.list_ebp()
        if ref in names:
            return ref
        hits = [n for n in names if n.endswith("/" + ref)
                or n.endswith("/" + ref + ".ebp")
                or os.path.basename(n) == ref
                or os.path.basename(n) == ref + ".ebp"]
        if len(hits) == 1:
            return hits[0]
        if not hits:
            raise KeyError("no .ebp matches %r" % ref)
        raise KeyError("ambiguous %r: %s" % (ref, hits[:8]))

    def read(self, ref):
        path = self.resolve(ref)
        if os.path.isfile(path):
            with open(path, "rb") as fh:
                return fh.read(), path
        return self.arc.read(path), path


# ------------------------------------------------------------------ text ------
#
# The FFX event text codec is only partly mapped.  Only the ranges verified by
# diffing the embedded US table against new_uspc/.../<name>.bin are decoded;
# everything else is shown as {XX} so nothing is invented.

_CHARSET = {0x3A: " ", 0x3B: ".", 0x41: "'", 0x4F: "?"}
for _i in range(26):
    _CHARSET[0x50 + _i] = chr(ord("A") + _i)
    _CHARSET[0x70 + _i] = chr(ord("a") + _i)


def decode_text(raw):
    out = []
    for b in raw:
        c = _CHARSET.get(b)
        out.append(c if c is not None else "{%02X}" % b)
    return "".join(out)


# --------------------------------------------------------------- opcodes ------

# name, number of operand-stack pops, short note.  "unk" where the semantics
# are not established.  Only the opcode numbers the interpreter implements are
# listed; anything else disassembles as op_XX.
OPCODES = {
    0x00: ("nop",        "no-op (shares the handler with 0x1D/0x1E/0x76)"),
    0x01: ("lor",        "int: push (a || b)"),
    0x02: ("land",       "int: push (a && b)"),
    0x03: ("or",         "int: push (a | b)"),
    0x04: ("xor",        "int: push (a ^ b)"),
    0x05: ("and",        "int: push (a & b)"),
    0x06: ("eq",         "push (a == b), int or float by stack tags"),
    0x07: ("ne",         "push (a != b)"),
    0x08: ("gt",         "push (a > b). Identical handler to 0x0A; never emitted"),
    0x09: ("lt",         "push (a < b). Identical handler to 0x0B; never emitted"),
    0x0A: ("gt",         "push (a > b), int or float by stack tags"),
    0x0B: ("lt",         "push (a < b)"),
    0x0C: ("ge",         "push (a >= b). Identical handler to 0x0E; never emitted"),
    0x0D: ("le",         "push (a <= b). Identical handler to 0x0F; never emitted"),
    0x0E: ("ge",         "push (a >= b)"),
    0x0F: ("le",         "push (a <= b)"),
    0x10: ("btst",       "push ((a >> b) & 1)"),
    0x11: ("btstz",      "push !((a >> b) & 1)"),
    0x12: ("shl",        "int: push (a << b)"),
    0x13: ("shr",        "int: push (a >> b)"),
    0x14: ("add",        "push (a + b), int or float by stack tags"),
    0x15: ("sub",        "push (a - b)"),
    0x16: ("mul",        "push (a * b)"),
    0x17: ("div",        "push (a / b)"),
    0x18: ("mod",        "int: push (a % b)"),
    0x19: ("lnot",       "int: push (a == 0)"),
    0x1A: ("fneg",       "float: push -a"),
    0x1C: ("bnot",       "int: push ~a"),
    0x1D: ("nop",        "no-op"),
    0x1E: ("nop",        "no-op"),
    0x1F: ("pushvar",    "push variable[operand][0]"),
    0x20: ("storevar",   "pop value -> variable[operand][0]"),
    0x21: ("storevar.s", "pop value -> variable[operand][0], saturating"),
    0x22: ("pushvar.ix", "pop index, push variable[operand][index]"),
    0x23: ("storevar.ix", "pop value, pop index -> variable[operand][index]"),
    0x24: ("storevar.ix.s", "pop value, pop index -> variable[operand][index], saturating"),
    0x25: ("popr",       "pop -> channel R register (ch+0x28). Always directly after a runscriptN: it takes that call's result"),
    0x26: ("pushr",      "push the channel R register (ch+0x28), i.e. read back the result a 0x58 syscall stashed"),
    0x27: ("pushvarref", "pop index, push a reference to variable[operand][index]"),
    0x28: ("pushcond",   "push the channel condition register (ch+0x20)"),
    0x29: ("pushsel",    "push the switch selector register (ch+0x24). 560102 of its 618561 uses are followed by eq"),
    0x2A: ("popcond",    "pop int -> channel condition register"),
    0x2B: ("dup",        "push a copy of the top of stack as int. Every one of its 12220 uses is followed by pushvar.ix"),
    0x2C: ("setsel",     "pop int -> switch selector register (ch+0x24). Every one of its 75364 uses is followed by jmp: this is the head of a switch"),
    0x2D: ("pushint",    "push intPool[operand]"),
    0x2E: ("pushimm",    "push (s16)operand"),
    0x2F: ("pushflt",    "push floatPool[operand]"),
    0x30: ("jmp",        "jump to label[operand]"),
    0x31: ("jnz",        "pop -> cond; jump to label[operand] if nonzero"),
    0x32: ("jz",         "pop -> cond; jump to label[operand] if zero"),
    0x33: ("callactor",  "call entry 0 of actor[operand] (pushes a return frame)"),
    0x34: ("ret",        "return from callactor"),
    0x35: ("syscallf",   "call system function; the result stays on the operand stack"),
    0x36: ("runscript0", "start another actor's script, 0 args; pushes a handle (always consumed by the next popr)"),
    0x37: ("runscript1", "start actor script, 1 arg"),
    0x38: ("runscript2", "start actor script, 2 args"),
    0x39: ("runparty0",  "start a party member's script, 0 args"),
    0x3A: ("runparty1",  "start a party member's script, 1 arg"),
    0x3B: ("runparty2",  "start a party member's script, 2 args"),
    0x3C: ("endscript",  "end this script (ctx+0x28 hook, else sub_8691D0 -1,0)"),
    0x3D: ("endscript.n", "pop n; end this script via sub_8691D0(n,0)"),
    0x3E: ("endscript.r", "end this script, resume variant (sub_8691D0 -1,1)"),
    0x3F: ("endscript.nr", "pop n; end via sub_8691D0(n,1)"),
    0x40: ("brk",        "debugger breakpoint check against ctx+0x64"),
    0x45: ("runscript0", "alias of 0x36"),
    0x46: ("runscript0", "alias of 0x36"),
    0x47: ("runscript0", "alias of 0x36"),
    0x48: ("runscript0", "alias of 0x36"),
    0x49: ("runscript0", "alias of 0x36"),
    0x4A: ("runscript1", "alias of 0x37"),
    0x4B: ("runscript1", "alias of 0x37"),
    0x4C: ("runscript1", "alias of 0x37"),
    0x4D: ("runscript1", "alias of 0x37"),
    0x4E: ("runscript1", "alias of 0x37"),
    0x4F: ("runscript2", "alias of 0x38"),
    0x50: ("runscript2", "alias of 0x38"),
    0x51: ("runscript2", "alias of 0x38"),
    0x52: ("runscript2", "alias of 0x38"),
    0x53: ("runscript2", "alias of 0x38"),
    0x54: ("yield",      "set flag 0x1000 and sub_8691D0(actor,0,0)"),
    0x55: ("jmp.p",      "pop -> cond; jump to label[operand] unconditionally"),
    0x56: ("jnz",        "pop -> cond; jump to label[operand] if nonzero"),
    0x57: ("jz",         "pop -> cond; jump to label[operand] if zero"),
    0x58: ("syscall",    "call system function; the result is popped into the channel R register instead of staying on the stack"),
    0x59: ("popi0",      "pop int -> int register 0 (actor+0x48)"),
    0x5A: ("popi1",      "pop int -> int register 1 (actor+0x4C)"),
    0x5B: ("popi2",      "pop int -> int register 2 (actor+0x50)"),
    0x5C: ("popi3",      "pop int -> int register 3 (actor+0x54)"),
    0x75: ("pushsys",    "push dword_C52704[operand]"),
    0x76: ("nop",        "no-op"),
    0x77: ("waitactor",  "pop script, pop actor; wait for that actor's script"),
    0x78: ("waitparty",  "pop script, pop party index; wait for that script"),
    0x79: ("op79",       "pop 3 ints -> sub_870D30(actor, a, b, c)"),
    0x7A: ("op7A",       "pop 2 ints -> push sub_86FAB0(actor, a, b)"),
}
for _i in range(10):
    OPCODES[0x5D + _i] = ("popf%d" % _i,
                          "pop float -> float register %d (actor+0x%02X)" % (_i, 0x58 + 4 * _i))
for _i in range(4):
    OPCODES[0x67 + _i] = ("pushi%d" % _i,
                          "push int register %d (actor+0x%02X)" % (_i, 0x48 + 4 * _i))
for _i in range(10):
    OPCODES[0x6B + _i] = ("pushf%d" % _i,
                          "push float register %d (actor+0x%02X)" % (_i, 0x58 + 4 * _i))

# opcodes whose operand is an index into the actor's jump label table
JUMP_OPS = (0x30, 0x31, 0x32, 0x55, 0x56, 0x57)
SYSCALL_OPS = (0x35, 0x58)

LIB_NAMES = {
    0: "core", 1: "sys", 2: "lib2", 3: "lib3", 4: "sg", 5: "ch", 6: "came",
    7: "btl", 8: "mapfx", 9: "test", 10: "lib10", 11: "movie", 12: "save",
    13: "abmap", 14: "lib14", 15: "lib15",
}

# Entries of g_ffxAtelSysFuncLib<NN>_* whose handler already has a real name in
# the IDA database.  (library, function index) -> handler name.
SYSCALL_NAMES = {
    (0, 0): "FFX_AtelOp_WaitFrames",
    (0, 1): "FFX_AtelOp_SetActorModel",
    (0, 21): "FFX_AtelOp_SetMoveTargetXYZ",
    (0, 24): "FFX_AtelOp_StartMove",
    (0, 25): "FFX_AtelOp_SetRotateCmd",
    (0, 93): "FFX_AtelOp_PlayerControlOn",
    (0, 94): "FFX_AtelOp_PlayerControlOff",
    (0, 114): "FFX_AtelOp_SetMoveFrames",
    (0, 152): "FFX_AtelOp_SetMoveSpline",
    (0, 213): "VoiceStandby",
    (0, 214): "VoiceStart",
    (0, 216): "VoiceStatus",
    (0, 217): "VoiceSync",
    (0, 247): "FFX_AtelOp_ResolvePartyMemberActor",
    (0, 413): "ChN_DataDispose",
    (0, 432): "premovie_load_result",
    (0, 453): "ReadAlBhedFromSaves",
    (4, 29): "sgMenu",
    (4, 37): "lgt",
    (4, 49): "Player_isMode2",
    (5, 12): "chEnGravity",
    (5, 16): "chReadSystemMGRP",
    (5, 23): "chSetGravMode",
    (5, 31): "chIsMotionActive",
    (5, 37): "chSetMotionSlot",
    (5, 40): "chSetRunThreshold",
    (5, 92): "chSetNextHokan",
    (5, 93): "chGetMotion",
    (5, 94): "chGetMoveSpeed",
    (5, 101): "chReadMotionGroupStart",
    (5, 102): "chReadMotionGroupSync",
    (6, 26): "CameraWait",
    (6, 59): "MsCameraSetScrDpt",
    (6, 78): "CameScreenOpen",
    (6, 79): "CameScreenClose",
    (6, 117): "CameScreenSetSlot",
    (6, 120): "Came_GetScrDpt",
    (7, 19): "BtlSetUnitMoveSpeed",
    (8, 10): "mpfpbindpos",
    (9, 0): "atel_test_print",
    (11, 2): "movie_stub2",
    (11, 11): "movie_force_destroy",
    (11, 12): "movie_end_frame_result",
    (11, 135): "sa_menu_init",
    (11, 136): "sa_menu_stop",
    (11, 143): "hdd_install_end",
    (11, 144): "hdd_format",
    (12, 0): "atelsaveram_read",
    (12, 1): "atelsaveram_write",
    (12, 11): "yiLoadBattleFieldParticleNo",
    (12, 12): "yiLoadFieldParticleNo",
    (12, 86): "saveram_read_group",
    (12, 87): "saveram_copy_to",
    (13, 0): "abiritymap_debug",
}

# Entry counts of the system-function libraries.  "used" is the highest index
# any of the 397 scripts actually calls; "table" is what the pointer array in
# .data supports.
LIB_INFO = {
    0:  (0xC50050, 616, 615),
    1:  (0xC52BE0, 30, 28),
    4:  (0xC88D88, 71, 70),
    5:  (0xC891F8, 145, 144),
    6:  (0xC43998, 138, 136),
    7:  (0xC42628, 235, 234),
    8:  (0xC5DC90, 108, 107),
    9:  (0xC5D8C0, 1, None),
    11: (0xC40E30, 145, 13),
    12: (0xC52DD8, 94, 91),
    13: (0xC85EB0, 1, None),
}


def syscall_label(op_id):
    lib = (op_id >> 12) & 0xFFF
    fn = op_id & 0xFFF
    name = SYSCALL_NAMES.get((lib, fn))
    base = "%s:%d" % (LIB_NAMES.get(lib, "lib%d" % lib), fn)
    return base + (" (%s)" % name if name else "")


# ------------------------------------------------------------------ model -----


class ActorDef(object):
    __slots__ = ("index", "offset", "raw", "type", "n_entries", "n_labels",
                 "var_tbl", "int_pool", "flt_pool", "entry_tbl", "label_tbl",
                 "class2", "class3", "class4", "entries", "labels")

    def __repr__(self):
        return "<ActorDef %d type=%d entries=%d labels=%d>" % (
            self.index, self.type, self.n_entries, self.n_labels)


class EbpFile(object):
    def __init__(self, data, name="<mem>"):
        self.data = data
        self.name = name
        if data[:2] != b"EV":
            raise ValueError("not an EV01 package (magic %r)" % data[:4])
        self.magic = data[:4]
        self.sections = []
        for i in range(1, 16):
            v = self._u32(4 * i)
            if v == 0xFFFFFFFF:
                break
            self.sections.append(v)
        else:
            raise ValueError("no -1 terminator in the container header")
        self.header_tail_clean = all(b == 0 for b in data[4 * (len(self.sections) + 2):0x40])
        self.data_end = self.sections[-1]
        self.at = self.sections[0]
        self._parse_atel()

    # --- raw helpers -------------------------------------------------------
    def _u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def _u16(self, off):
        return struct.unpack_from("<H", self.data, off)[0]

    def au32(self, off):
        return struct.unpack_from("<I", self.data, self.at + off)[0]

    def au16(self, off):
        return struct.unpack_from("<H", self.data, self.at + off)[0]

    def as32(self, off):
        return struct.unpack_from("<i", self.data, self.at + off)[0]

    def astr(self, off, limit=64):
        base = self.at + off
        end = self.data.find(b"\0", base, base + limit)
        return self.data[base:end if end >= 0 else base + limit].decode("latin1")

    # --- container ---------------------------------------------------------
    def section(self, idx):
        """(offset, size) of container section idx, or None when absent."""
        if idx >= len(self.sections) - 1:
            return None
        off = self.sections[idx]
        if off == 0:
            return None
        later = [x for x in self.sections[idx + 1:] if x]
        end = min(later) if later else len(self.data)
        return off, end - off

    # --- atel block --------------------------------------------------------
    def _parse_atel(self):
        self.atel_unk00 = self.au32(0x00)
        self.map_entry_tbl = self.au32(0x04)
        self.name_a = self.au32(0x08)
        self.name_b = self.au32(0x0C)
        self.atel_size = self.au32(0x10)
        self.n_1464 = self.au16(0x14)
        self.n_1368 = self.au16(0x16)
        self.atel_unk18 = self.au16(0x18)
        self.atel_unk1a = self.au16(0x1A)
        self.n_744 = self.au16(0x1C)
        self.atel_unk1e = self.au16(0x1E)
        self.class6_base = self.au32(0x20)
        self.atel_unk24 = self.au32(0x24)
        self.atel_unk28 = self.au32(0x28)
        self.res = self.au32(0x2C)
        self.code = self.au32(0x30)
        self.n_actors = self.au16(0x34)
        self.n_large = self.au16(0x36)
        self.n_chars = self.n_large - self.n_1464 - self.n_1368 - self.n_744

        self.actor_offsets = list(struct.unpack_from(
            "<%dI" % self.n_actors, self.data, self.at + 0x38))

        self.actors = []
        for i, ao in enumerate(self.actor_offsets):
            a = ActorDef()
            a.index = i
            a.offset = ao
            b = self.at + ao
            a.raw = self.data[b:b + 52]
            a.type = a.raw[0]
            a.n_entries, a.n_labels = struct.unpack_from("<HH", a.raw, 8)
            (a.var_tbl, a.int_pool, a.flt_pool, a.entry_tbl, a.label_tbl,
             a.class2, a.class3, a.class4) = struct.unpack_from("<8I", a.raw, 0x14)
            a.entries = list(struct.unpack_from(
                "<%dI" % a.n_entries, self.data, self.at + a.entry_tbl)) if a.n_entries else []
            a.labels = list(struct.unpack_from(
                "<%dI" % a.n_labels, self.data, self.at + a.label_tbl)) if a.n_labels else []
            self.actors.append(a)

        self.code_end = self._find_code_end()

    def _find_code_end(self):
        """Smallest known structural offset greater than the code base.

        Verified: a linear instruction sweep from self.code lands exactly on
        this value in all 397 archive files.
        """
        cand = [self.atel_size]
        for off in (0x04, 0x08, 0x0C, 0x20, 0x24, 0x28, 0x2C):
            v = self.au32(off)
            if v:
                cand.append(v)
        for a in self.actors:
            for v in (a.var_tbl, a.int_pool, a.flt_pool, a.entry_tbl,
                      a.label_tbl, a.class2, a.class3, a.class4):
                if v:
                    cand.append(v)
        return min(x for x in cand if x > self.code)

    # --- pools -------------------------------------------------------------
    def var_table(self, actor=0):
        a = self.actors[actor]
        if not a.var_tbl:
            return []
        n = (a.int_pool - a.var_tbl) // 8 if a.int_pool > a.var_tbl else 0
        out = []
        for i in range(n):
            desc, cnt, unk = struct.unpack_from("<IHH", self.data,
                                                self.at + a.var_tbl + 8 * i)
            out.append((i, desc, cnt, unk))
        return out

    def int_pool(self, actor=0):
        a = self.actors[actor]
        if not a.int_pool or a.flt_pool <= a.int_pool:
            return []
        n = (a.flt_pool - a.int_pool) // 4
        return list(struct.unpack_from("<%di" % n, self.data, self.at + a.int_pool))

    def float_pool(self, actor=0):
        a = self.actors[actor]
        if not a.flt_pool or self.code <= a.flt_pool:
            return []
        n = (self.code - a.flt_pool) // 4
        return list(struct.unpack_from("<%df" % n, self.data, self.at + a.flt_pool))

    # --- messages ----------------------------------------------------------
    def messages(self, which="us"):
        """[(index, offsetA, attrA, offsetB, attrB, rawA)].

        Record layout confirmed from sub_86BF30 / sub_86BF10, which index the
        table with 8 bytes per message id: u16 string offset at +0 read by
        sub_86BF30, u16 attribute at +2 read by sub_86BF10, then a second
        {offset, attribute} pair at +4/+6.  Across all 397 files the two pairs
        are identical in 40125 of 40376 records.  The first u16 of the block is
        the size of the record array, so the message count is that over 8.
        """
        idx = 4 if which == "us" else 1
        sec = self.section(idx)
        if not sec:
            return []
        off, size = sec
        blk = self.data[off:off + size]
        if len(blk) < 8:
            return []
        first = struct.unpack_from("<H", blk, 0)[0]
        if first == 0 or first % 8 or first > len(blk):
            return []
        out = []
        for i in range(first // 8):
            a, aa, b, ba = struct.unpack_from("<4H", blk, 8 * i)
            e = blk.find(bytes(1), a) if a < len(blk) else -1
            raw = blk[a:e if e >= 0 else len(blk)]
            out.append((i, a, aa, b, ba, raw))
        return out

    # --- code --------------------------------------------------------------
    def instructions(self, start=0, end=None):
        """Yield (code-relative offset, length, opcode, operand_or_None)."""
        base = self.at + self.code
        p = start
        stop = (self.code_end - self.code) if end is None else end
        d = self.data
        while p < stop:
            b0 = d[base + p]
            if b0 & 0x80:
                yield p, 3, b0 & 0x7F, d[base + p + 1] | (d[base + p + 2] << 8)
                p += 3
            else:
                yield p, 1, b0, None
                p += 1

    def entry_owner_map(self):
        """offset -> list of (actor index, 'E'/'L', slot) for every entry and label."""
        m = {}
        for a in self.actors:
            for k, v in enumerate(a.entries):
                m.setdefault(v, []).append((a.index, "E", k))
            for k, v in enumerate(a.labels):
                m.setdefault(v, []).append((a.index, "L", k))
        return m

    def actor_of_offset(self):
        """Attribute each code offset to an actor using sorted entry points."""
        marks = sorted({(v, a.index) for a in self.actors for v in a.entries})
        return marks


# ----------------------------------------------------------------- output -----


def cmd_info(src, ref):
    data, path = src.read(ref)
    f = EbpFile(data, path)
    print("file          %s" % path)
    print("size          %d (0x%X)" % (len(data), len(data)))
    print("magic         %r" % f.magic)
    print("header tail zero: %s" % f.header_tail_clean)
    print()
    print("container sections (%d data sections + end sentinel):" % (len(f.sections) - 1))
    print("  %-4s %-10s %-10s %-10s %s" % ("#", "hdr off", "offset", "size", "role"))
    for i in range(len(f.sections) - 1):
        sec = f.section(i)
        role = SECTION_NAMES[i] if i < len(SECTION_NAMES) else "?"
        if sec is None:
            print("  %-4d +0x%-8X %-10s %-10s %s (absent)" % (i, 4 + 4 * i, "-", "-", role))
        else:
            off, size = sec
            extra = ""
            if role == "se_sesep":
                extra = "  tag=%r" % data[off + 0x20:off + 0x28]
            if role == "ftcx":
                extra = "  tag=%r" % data[off:off + 4]
            print("  %-4d +0x%-8X 0x%-8X 0x%-8X %s%s" % (i, 4 + 4 * i, off, size, role, extra))
    print("  end  +0x%-8X 0x%-8X %-10s end-of-data sentinel%s"
          % (4 + 4 * (len(f.sections) - 1), f.data_end, "",
             " (== file size)" if f.data_end == len(data) else " (!= file size)"))
    print()
    print("ATEL block at file 0x%X, size 0x%X" % (f.at, f.atel_size))
    print("  +0x00 unk            0x%08X" % f.atel_unk00)
    print("  +0x04 map entry tbl  0x%X" % f.map_entry_tbl)
    print("  +0x08 name A         0x%-8X %r" % (f.name_a, f.astr(f.name_a)))
    print("  +0x0C name B         0x%-8X %r" % (f.name_b, f.astr(f.name_b)))
    print("  +0x10 block size     0x%X" % f.atel_size)
    print("  +0x14 n(1464)        %d" % f.n_1464)
    print("  +0x16 n(1368)        %d" % f.n_1368)
    print("  +0x18 unk            %d" % f.atel_unk18)
    print("  +0x1A unk            %d" % f.atel_unk1a)
    print("  +0x1C n(744)         %d" % f.n_744)
    print("  +0x1E unk flags      0x%04X" % f.atel_unk1e)
    print("  +0x20 class6 base    0x%X" % f.class6_base)
    print("  +0x24 unk            0x%X" % f.atel_unk24)
    print("  +0x28 unk 16xu16     0x%X" % f.atel_unk28)
    print("  +0x2C RES            0x%X" % f.res)
    print("  +0x30 code           0x%X .. 0x%X  (%d bytes)"
          % (f.code, f.code_end, f.code_end - f.code))
    print("  +0x34 actors         %d" % f.n_actors)
    print("  +0x36 large actors   %d  (characters=%d, 1464=%d, 1368=%d, 744=%d, 48=%d)"
          % (f.n_large, f.n_chars, f.n_1464, f.n_1368, f.n_744, f.n_actors - f.n_large))
    print("  pool bytes           %d" % (2904 * f.n_chars + 1368 * f.n_1368
                                         + 1464 * f.n_1464 + 744 * f.n_744
                                         + 48 * (f.n_actors - f.n_large)))
    if f.res:
        print()
        print("RES sub-header at atel+0x%X:" % f.res)
        for lbl, off, kind in (("spare CHRs", 0x04, "u32"), ("flags", 0x08, "x32"),
                               ("resource list", 0x0C, "off"),
                               ("sound grp A", 0x1C, "off"), ("sound grp B", 0x20, "off"),
                               ("block 0x2C", 0x2C, "off"),
                               ("sound grp C", 0x30, "off"), ("sound grp D", 0x34, "off"),
                               ("bgm word", 0x38, "x32"), ("sub-table T", 0x3C, "off")):
            v = f.au32(f.res + off)
            if kind == "x32":
                print("  +0x%02X %-14s 0x%08X%s" % (off, lbl, v,
                      "  bgm id %d" % (v & 0xFFFF) if off == 0x38 and v & 0x80000000 else ""))
            else:
                print("  +0x%02X %-14s 0x%X" % (off, lbl, v))
        rl = f.au32(f.res + 0x0C)
        if rl:
            n = f.au32(rl)
            print("  resource list: %d entries {u32 id, u16 flags}" % n)
            for i in range(min(n, 12)):
                rid = f.au32(rl + 4 + 8 * i)
                fl = f.au32(rl + 8 + 8 * i) & 0xFFFF
                print("    [%3d] id 0x%06X flags 0x%04X" % (i, rid, fl))
            if n > 12:
                print("    ... %d more" % (n - 12))


def cmd_actors(src, ref):
    data, path = src.read(ref)
    f = EbpFile(data, path)
    print("%s: %d actors (%d large)" % (path, f.n_actors, f.n_large))
    print("%-4s %-8s %-4s %-7s %-7s %-8s %-8s %-8s %-8s %-8s"
          % ("#", "off", "typ", "entries", "labels", "vartbl", "intpool", "fltpool", "entrytbl", "labeltbl"))
    for a in f.actors:
        print("%-4d 0x%-6X %-4d %-7d %-7d 0x%-6X 0x%-6X 0x%-6X 0x%-6X 0x%-6X"
              % (a.index, a.offset, a.type, a.n_entries, a.n_labels,
                 a.var_tbl, a.int_pool, a.flt_pool, a.entry_tbl, a.label_tbl))
    print()
    print("entry points (code offsets):")
    for a in f.actors:
        if a.entries:
            print("  actor %-3d E: %s" % (a.index, " ".join("0x%X" % x for x in a.entries)))
        if a.labels:
            print("  actor %-3d L: %s" % (a.index, " ".join("0x%X" % x for x in a.labels)))


def cmd_vars(src, ref):
    data, path = src.read(ref)
    f = EbpFile(data, path)
    tys = {0: "u8", 1: "s8", 2: "u16", 3: "s16", 4: "u32", 5: "s32", 6: "f32"}
    cls = {0: "globalA", 1: "globalB", 2: "actor.c2", 3: "actor.c3",
           4: "actor.c4", 5: "reg", 6: "atel.c6"}
    rows = f.var_table(0)
    print("%s: %d variable descriptors (shared by all actors in the file)" % (path, len(rows)))
    print("%-5s %-10s %-5s %-9s %-10s %-6s %s" % ("#", "desc", "type", "class", "offset", "count", "unk"))
    for i, desc, cnt, unk in rows:
        print("%-5d 0x%08X %-5s %-9s 0x%-8X %-6d 0x%04X"
              % (i, desc, tys.get(desc >> 28, "?%d" % (desc >> 28)),
                 cls.get((desc >> 25) & 7, "?"), desc & 0xFFFFFF, cnt, unk))


def cmd_consts(src, ref):
    data, path = src.read(ref)
    f = EbpFile(data, path)
    ip = f.int_pool(0)
    fp = f.float_pool(0)
    print("%s: int pool %d entries, float pool %d entries" % (path, len(ip), len(fp)))
    for i, v in enumerate(ip):
        print("  int  [%4d] %11d  0x%08X" % (i, v, v & 0xFFFFFFFF))
    for i, v in enumerate(fp):
        print("  flt  [%4d] %g" % (i, v))


def cmd_text(src, ref):
    data, path = src.read(ref)
    f = EbpFile(data, path)
    for which, label in (("jp", "built-in JP table (section 1)"),
                         ("us", "built-in US table (section 4)")):
        msgs = f.messages(which)
        print("== %s: %d messages" % (label, len(msgs)))
        for i, a, aa, b, ba, raw in msgs:
            alt = "" if (a, aa) == (b, ba) else "  [alt +0x%04X attr 0x%04X]" % (b, ba)
            print("  [%4d] +0x%04X attr 0x%04X%s %s"
                  % (i, a, aa, alt, decode_text(raw)))
        print()


def cmd_dis(src, ref, only_actor=None):
    data, path = src.read(ref)
    f = EbpFile(data, path)
    owners = f.entry_owner_map()
    print("; %s" % path)
    print("; code 0x%X .. 0x%X  (%d bytes), %d actors"
          % (f.code, f.code_end, f.code_end - f.code, f.n_actors))
    print("; offsets below are relative to the code base (atel+0x%X)" % f.code)
    print()
    ip = f.int_pool(0)
    fp = f.float_pool(0)
    cur_actor = None
    for off, ln, op, oper in f.instructions():
        for owner in owners.get(off, []):
            idx, kind, slot = owner
            if kind == "E":
                cur_actor = idx
            print("%s%s_%d_%d:" % ("\n" if kind == "E" else "",
                                   "entry" if kind == "E" else "label", idx, slot))
        if only_actor is not None and cur_actor != only_actor:
            continue
        raw = f.data[f.at + f.code + off:f.at + f.code + off + ln]
        name, note = OPCODES.get(op, (None, None))
        if name is None:
            text = "op_%02X" % op + ("" if oper is None else " 0x%04X" % oper)
            text += "        ; UNKNOWN opcode"
        elif op in SYSCALL_OPS:
            text = "%-14s %s" % (name, syscall_label(oper))
        elif op in JUMP_OPS:
            a = f.actors[cur_actor] if cur_actor is not None else None
            tgt = ("0x%X" % a.labels[oper]) if (a and oper < len(a.labels)) else "?"
            text = "%-14s label_%s_%d   ; -> %s" % (
                name, cur_actor if cur_actor is not None else "?", oper, tgt)
        elif op == 0x2D:
            v = ip[oper] if oper < len(ip) else None
            text = "%-14s %d%s" % (name, oper, "   ; %d" % v if v is not None else "")
        elif op == 0x2F:
            v = fp[oper] if oper < len(fp) else None
            text = "%-14s %d%s" % (name, oper, "   ; %g" % v if v is not None else "")
        elif op == 0x2E:
            text = "%-14s %d" % (name, struct.unpack("<h", struct.pack("<H", oper))[0])
        elif op == 0x33:
            text = "%-14s actor %d" % (name, oper)
            text += "   ; -> entry_%d_0" % oper
        elif oper is not None:
            text = "%-14s %d" % (name, oper)
        else:
            text = name
        print("  %06X  %-8s  %s" % (off, raw.hex(), text))


def cmd_verify(src):
    names = src.list_ebp()
    ok = 0
    problems = []
    import collections
    opcount = collections.Counter()
    libmax = collections.defaultdict(int)
    nsect = collections.Counter()
    for n in names:
        data = src.arc.read(n)
        try:
            f = EbpFile(data, n)
        except Exception as e:
            problems.append((n, "parse: %r" % e))
            continue
        nsect[len(f.sections) - 1] += 1
        bad = []
        if not f.header_tail_clean:
            bad.append("header tail not zero")
        bounds = set()
        p = 0
        d = f.data
        base = f.at + f.code
        limit = f.code_end - f.code
        while p < limit:
            bounds.add(p)
            b0 = d[base + p]
            if b0 & 0x80:
                op = b0 & 0x7F
                oper = d[base + p + 1] | (d[base + p + 2] << 8)
                p += 3
            else:
                op = b0
                oper = None
                p += 1
            opcount[op] += 1
            if op in SYSCALL_OPS:
                libmax[oper >> 12] = max(libmax[oper >> 12], oper & 0xFFF)
        if p != limit:
            bad.append("sweep overran to 0x%X (want 0x%X)" % (p, limit))
        for a in f.actors:
            for v in a.entries + a.labels:
                if v not in bounds:
                    bad.append("actor %d target 0x%X off-boundary" % (a.index, v))
                    break
            if a.entry_tbl + 4 * a.n_entries != a.label_tbl:
                bad.append("actor %d entry/label tables not adjacent" % a.index)
        if f.actors:
            tbl_end = max(max(a.entry_tbl + 4 * a.n_entries,
                              a.label_tbl + 4 * a.n_labels) for a in f.actors)
            pad = f.atel_size - tbl_end
            if not 0 <= pad <= 12:
                bad.append("tables end at 0x%X, block size 0x%X (pad %d)"
                           % (tbl_end, f.atel_size, pad))
        if bad:
            problems.append((n, "; ".join(bad[:3])))
        else:
            ok += 1
    print("parsed and structurally verified: %d / %d" % (ok, len(names)))
    print("data sections per file: %s" % dict(nsect))
    if problems:
        print("problems:")
        for n, why in problems[:30]:
            print("  %-60s %s" % (n.split("/")[-1], why))
    print()
    print("opcodes seen in real scripts:")
    for op in sorted(opcount):
        name = OPCODES.get(op, ("op_%02X" % op,))[0]
        print("  0x%02X %-16s %10d" % (op, name, opcount[op]))
    print()
    print("highest system-function index used per library:")
    for lib in sorted(libmax):
        tbl = LIB_INFO.get(lib, (0, None, None))[1]
        print("  lib %-2d (%-6s) max %-5d  table entries %s"
              % (lib, LIB_NAMES.get(lib, "?"), libmax[lib], tbl))


# ------------------------------------------------------------------- main -----


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[1]
    src = Source()
    if cmd == "list":
        for n in src.list_ebp():
            print(n)
        return 0
    if cmd == "verify":
        cmd_verify(src)
        return 0
    if len(argv) < 3:
        print("%s needs a file name" % cmd)
        return 1
    ref = argv[2]
    if cmd == "info":
        cmd_info(src, ref)
    elif cmd == "actors":
        cmd_actors(src, ref)
    elif cmd == "vars":
        cmd_vars(src, ref)
    elif cmd == "consts":
        cmd_consts(src, ref)
    elif cmd == "text":
        cmd_text(src, ref)
    elif cmd == "dis":
        a = None
        if "-a" in argv:
            a = int(argv[argv.index("-a") + 1])
        cmd_dis(src, ref, a)
    else:
        print("unknown command %r" % cmd)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
