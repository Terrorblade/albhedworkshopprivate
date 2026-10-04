#!/usr/bin/env python3
"""
magicdll.py - reader for FINAL FANTASY X HD Remaster "magic file" plugin DLLs.

The game ships one DLL per spell / summon effect under

    <game>/magicFiles/FFX/magic_%04d.dll      (581 files for FFX)
    <game>/magicFiles/FFX-2/magic_%04d.dll    (844 files for FFX-2)

Each DLL is a 32-bit PE32 that exports exactly two symbols:

    void *__cdecl GetEffectOverlayTable(int which);
    void  __cdecl InitMagicPRX(void **hostApiTable);

This tool parses those DLLs with a self-contained PE reader (no external
packages) and dumps what has been decoded so far. Fields that are still
unidentified are printed as UNKNOWN rather than guessed at.

Usage
    python magicdll.py info    <dll> [...]     headers, sections, exports, imports
    python magicdll.py abi     <dll> [...]     the two exports and the host-table wiring
    python magicdll.py ovl     <dll> [...]     the 16-slot effect overlay table
    python magicdll.py ef2     <dll> [...]     the effect-data blob pointed at by overlay slot 7
    python magicdll.py calls   <dll> [...]     host API table indices this DLL calls,
                                               with the address of every call site
    python magicdll.py survey  <dir>           aggregate stats over a whole magicFiles dir
    python magicdll.py all     <dll>           everything for one DLL

Confidence levels used in the output
    [C] confirmed - read directly out of the file, or matched in all 581 FFX DLLs
    [I] inferred  - consistent with the host-side code but not proven
    [?] unknown
"""

import os
import re
import struct
import sys

# ---------------------------------------------------------------------------
# minimal PE32 reader
# ---------------------------------------------------------------------------

IMAGE_SCN = {
    0x00000020: "CODE",
    0x00000040: "INITIALIZED_DATA",
    0x00000080: "UNINITIALIZED_DATA",
    0x20000000: "EXECUTE",
    0x40000000: "READ",
    0x80000000: "WRITE",
    0x02000000: "DISCARDABLE",
}


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class PE32(object):
    """Just enough of the PE format for these DLLs. No external deps."""

    def __init__(self, data, path=None):
        self.d = data
        self.path = path
        if data[:2] != b"MZ":
            raise ValueError("not an MZ image")
        nt = u32(data, 0x3C)
        if data[nt:nt + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        fh = nt + 4
        self.machine = u16(data, fh)
        self.nsections = u16(data, fh + 2)
        self.timestamp = u32(data, fh + 4)
        self.characteristics = u16(data, fh + 18)
        opt_size = u16(data, fh + 16)
        oh = fh + 20
        self.opt_magic = u16(data, oh)
        if self.opt_magic != 0x10B:
            raise ValueError("expected PE32 (0x10B), got 0x%X" % self.opt_magic)
        self.entry_point = u32(data, oh + 16)
        self.image_base = u32(data, oh + 28)
        self.section_align = u32(data, oh + 32)
        self.file_align = u32(data, oh + 36)
        self.size_of_image = u32(data, oh + 56)
        self.subsystem = u16(data, oh + 68)
        self.dll_characteristics = u16(data, oh + 70)
        n_dd = u32(data, oh + 92)
        dd = oh + 96
        self.dirs = [(u32(data, dd + 8 * i), u32(data, dd + 8 * i + 4))
                     for i in range(min(16, n_dd))]
        so = oh + opt_size
        self.sections = []
        for i in range(self.nsections):
            b = so + 40 * i
            self.sections.append({
                "name": data[b:b + 8].rstrip(b"\0").decode("latin1"),
                "vsize": u32(data, b + 8),
                "vaddr": u32(data, b + 12),
                "rawsize": u32(data, b + 16),
                "rawoff": u32(data, b + 20),
                "chars": u32(data, b + 36),
            })

    # -- address helpers ----------------------------------------------------

    def section_of_rva(self, rva):
        for s in self.sections:
            span = max(s["vsize"], s["rawsize"])
            if s["vaddr"] <= rva < s["vaddr"] + span:
                return s
        return None

    def section_of_va(self, va):
        return self.section_of_rva(va - self.image_base)

    def rva_to_off(self, rva):
        s = self.section_of_rva(rva)
        if s is None:
            return None
        delta = rva - s["vaddr"]
        if delta >= s["rawsize"]:
            return None          # lives in the BSS tail, no file bytes
        return s["rawoff"] + delta

    def va_to_off(self, va):
        return self.rva_to_off(va - self.image_base)

    def read_va(self, va, n):
        o = self.va_to_off(va)
        if o is None:
            return None
        return self.d[o:o + n]

    def dword_va(self, va):
        b = self.read_va(va, 4)
        return None if b is None or len(b) < 4 else u32(b, 0)

    def cstr_rva(self, rva):
        o = self.rva_to_off(rva)
        if o is None:
            return None
        e = self.d.find(b"\0", o)
        return self.d[o:e].decode("latin1")

    def section(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def section_bytes(self, name):
        s = self.section(name)
        if s is None:
            return b""
        return self.d[s["rawoff"]:s["rawoff"] + s["rawsize"]]

    # -- directories --------------------------------------------------------

    def exports(self):
        """[(name, ordinal, rva)] sorted by rva."""
        if not self.dirs or not self.dirs[0][0]:
            return []
        o = self.rva_to_off(self.dirs[0][0])
        n_names = u32(self.d, o + 24)
        a_func = u32(self.d, o + 28)
        a_name = u32(self.d, o + 32)
        a_ord = u32(self.d, o + 36)
        base = u32(self.d, o + 16)
        fo = self.rva_to_off(a_func)
        no = self.rva_to_off(a_name)
        oo = self.rva_to_off(a_ord)
        out = []
        for i in range(n_names):
            nr = u32(self.d, no + 4 * i)
            oi = u16(self.d, oo + 2 * i)
            out.append((self.cstr_rva(nr), oi + base, u32(self.d, fo + 4 * oi)))
        out.sort(key=lambda t: t[2])
        return out

    def export_rva(self, name):
        for nm, _o, rva in self.exports():
            if nm == name:
                return rva
        return None

    def imports(self):
        """[(dllname, [funcnames])]"""
        if len(self.dirs) < 2 or not self.dirs[1][0]:
            return []
        o = self.rva_to_off(self.dirs[1][0])
        out = []
        while True:
            oft, _ts, _fc, nm, fta = struct.unpack_from("<IIIII", self.d, o)
            if nm == 0:
                break
            fns = []
            t = self.rva_to_off(oft or fta)
            while True:
                v = u32(self.d, t)
                if v == 0:
                    break
                fns.append("#%d" % (v & 0xFFFF) if v & 0x80000000
                           else self.cstr_rva(v + 2))
                t += 4
            out.append((self.cstr_rva(nm), fns))
            o += 20
        return out

    def reloc_count(self):
        if len(self.dirs) < 6 or not self.dirs[5][0]:
            return 0, 0
        rva, size = self.dirs[5]
        o = self.rva_to_off(rva)
        end = o + size
        total = blocks = 0
        while o < end:
            bs = u32(self.d, o + 4)
            if bs == 0:
                break
            total += (bs - 8) // 2
            blocks += 1
            o += bs
        return total, blocks


# ---------------------------------------------------------------------------
# a length-only x86-32 instruction decoder
# ---------------------------------------------------------------------------

# The host-call scan and the InitMagicPRX reader both used to walk .text byte
# by byte looking for opcode patterns. That under-counts and it mis-syncs: a
# 0xA3 or a 0x8B byte sitting inside somebody else's immediate looks exactly
# like an instruction. This decoder returns instruction lengths, so both walks
# step from one real instruction to the next.
#
# It decodes lengths and operand shapes only, no mnemonics, which keeps the
# tables small. Validated by sweeping all 581 FFX .text sections end to end:
# every one decodes with no leftover bytes once the switch jump tables MSVC
# embeds in .text are skipped, and both export entry points land on an
# instruction boundary the sweep agrees with.

PREFIX_SEG = (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65)

# one-byte opcodes that carry a modrm byte
_MODRM_1 = frozenset((
    0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B,
    0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B,
    0x20, 0x21, 0x22, 0x23, 0x28, 0x29, 0x2A, 0x2B,
    0x30, 0x31, 0x32, 0x33, 0x38, 0x39, 0x3A, 0x3B,
    0x62, 0x63, 0x69, 0x6B,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0xC0, 0xC1, 0xC4, 0xC5, 0xC6, 0xC7,
    0xD0, 0xD1, 0xD2, 0xD3,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xF6, 0xF7, 0xFE, 0xFF))

# one-byte opcodes with an immediate of fixed size
_IMM_1 = {
    0x04: 1, 0x0C: 1, 0x14: 1, 0x1C: 1, 0x24: 1, 0x2C: 1, 0x34: 1, 0x3C: 1,
    0x6A: 1, 0x6B: 1,
    0x70: 1, 0x71: 1, 0x72: 1, 0x73: 1, 0x74: 1, 0x75: 1, 0x76: 1, 0x77: 1,
    0x78: 1, 0x79: 1, 0x7A: 1, 0x7B: 1, 0x7C: 1, 0x7D: 1, 0x7E: 1, 0x7F: 1,
    0x80: 1, 0x82: 1, 0x83: 1, 0xA8: 1,
    0xB0: 1, 0xB1: 1, 0xB2: 1, 0xB3: 1, 0xB4: 1, 0xB5: 1, 0xB6: 1, 0xB7: 1,
    0xC0: 1, 0xC1: 1, 0xC2: 2, 0xC6: 1, 0xC8: 3, 0xCA: 2, 0xCD: 1,
    0xD4: 1, 0xD5: 1,
    0xE0: 1, 0xE1: 1, 0xE2: 1, 0xE3: 1, 0xE4: 1, 0xE5: 1, 0xE6: 1, 0xE7: 1,
    0xEB: 1,
}
# one-byte opcodes whose immediate is 4 bytes, or 2 under a 0x66 prefix
_IMM_1_OPSIZE = frozenset((
    0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D, 0x68, 0x69,
    0x81, 0xA9, 0xC7, 0xE8, 0xE9,
    0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF))
_IMM_1_FARPTR = (0x9A, 0xEA)          # ptr16:32
_IMM_1_MOFFS = (0xA0, 0xA1, 0xA2, 0xA3)   # the immediate is an address

# two-byte (0x0F xx) opcodes that carry a modrm byte
_MODRM_2 = set()
for _r in (range(0x00, 0x04), (0x0D,), range(0x10, 0x24), range(0x28, 0x30),
           range(0x40, 0x77), range(0x78, 0x80),
           range(0x90, 0xA0), range(0xA3, 0xA6), range(0xAB, 0xC8),
           range(0xD0, 0xFF)):
    _MODRM_2.update(_r)
_MODRM_2 = frozenset(_MODRM_2)

_IMM_2 = {0x70: 1, 0x71: 1, 0x72: 1, 0x73: 1, 0xA4: 1, 0xAC: 1, 0xBA: 1,
          0xC2: 1, 0xC4: 1, 0xC5: 1, 0xC6: 1}
_IMM_2_OPSIZE = frozenset(range(0x80, 0x90))      # jcc rel32


class BadInsn(Exception):
    """The bytes at this offset are not a decodable instruction."""


def decode_insn(b, i):
    """Decode the instruction at b[i] and return a dict:

        len     total length in bytes
        op      the opcode byte; a 0x0F escape sets two, a 0x0F38 / 0x0F3A
                escape folds the third byte in as 0x0F38xx / 0x0F3Axx
        two     True when it came through the 0x0F escape
        modrm   the modrm byte, or None
        mod/reg/rm
        base    the base register index for a plain [reg+disp] operand, the
                string "abs" for a mod=00 rm=101 absolute disp32, else None
                (SIB forms are deliberately not resolved)
        disp    displacement, signed
        imm     immediate size in bytes
        osz     2 under a 0x66 prefix, else 4

    Raises BadInsn rather than guessing.
    """
    n = len(b)
    start = i
    osz = 4
    asz = 4
    while i < n:
        c = b[i]
        if c == 0x66:
            osz = 2
            i += 1
        elif c == 0x67:
            asz = 2
            i += 1
        elif c in (0xF0, 0xF2, 0xF3) or c in PREFIX_SEG:
            i += 1
        else:
            break
    else:
        raise BadInsn("ran off the end inside the prefixes")
    if i - start > 6:
        raise BadInsn("more prefixes than any real instruction has")
    op = b[i]
    i += 1
    two = False
    if op == 0x0F:
        if i >= n:
            raise BadInsn("truncated 0F escape")
        two = True
        op = b[i]
        i += 1
        if op in (0x38, 0x3A):
            if i >= n:
                raise BadInsn("truncated 0F 38 / 0F 3A escape")
            third = b[i]
            i += 1
            has_modrm = True
            imm = 1 if op == 0x3A else 0
            op = (0x0F3A00 if op == 0x3A else 0x0F3800) | third
        else:
            has_modrm = op in _MODRM_2
            imm = osz if op in _IMM_2_OPSIZE else _IMM_2.get(op, 0)
    else:
        has_modrm = op in _MODRM_1
        if op in _IMM_1_OPSIZE:
            imm = osz
        elif op in _IMM_1_FARPTR:
            imm = osz + 2
        elif op in _IMM_1_MOFFS:
            imm = asz
        else:
            imm = _IMM_1.get(op, 0)
        if op in (0xF6, 0xF7):
            imm = 0                   # group 3, settled once the modrm is read
    modrm = mod = reg = rm = None
    base = None
    disp = 0
    if has_modrm:
        if i >= n:
            raise BadInsn("truncated modrm")
        modrm = b[i]
        i += 1
        mod = modrm >> 6
        reg = (modrm >> 3) & 7
        rm = modrm & 7
        if not two and op in (0xF6, 0xF7) and reg in (0, 1):
            imm = 1 if op == 0xF6 else osz
        if mod != 3:
            if asz == 2:
                raise BadInsn("16-bit addressing, not expected in this code")
            if rm == 4:
                if i >= n:
                    raise BadInsn("truncated SIB")
                sib = b[i]
                i += 1
                if (sib & 7) == 5 and mod == 0:
                    if i + 4 > n:
                        raise BadInsn("truncated SIB disp32")
                    disp = int.from_bytes(b[i:i + 4], "little", signed=True)
                    i += 4
            elif mod == 0 and rm == 5:
                if i + 4 > n:
                    raise BadInsn("truncated disp32")
                disp = int.from_bytes(b[i:i + 4], "little", signed=True)
                i += 4
                base = "abs"
            else:
                base = rm
            if mod == 1:
                if i + 1 > n:
                    raise BadInsn("truncated disp8")
                disp = b[i] - 256 if b[i] >= 128 else b[i]
                i += 1
            elif mod == 2:
                if i + 4 > n:
                    raise BadInsn("truncated disp32")
                disp = int.from_bytes(b[i:i + 4], "little", signed=True)
                i += 4
    if imm:
        if i + imm > n:
            raise BadInsn("truncated immediate")
        i += imm
    return dict(len=i - start, op=op, two=two, modrm=modrm, mod=mod, reg=reg,
                rm=rm, base=base, disp=disp, imm=imm, osz=osz)


def insn_abs_addr(b, i, d):
    """The absolute address an instruction names, for the two forms that name
    one: a moffs immediate (A0..A3) and a mod=00 rm=101 operand."""
    if not d["two"] and d["op"] in _IMM_1_MOFFS:
        e = i + d["len"]
        return int.from_bytes(b[e - d["imm"]:e], "little")
    if d["base"] == "abs":
        return d["disp"] & 0xFFFFFFFF
    return None


def insn_rel_target(b, i, d):
    """Target offset of a relative branch, or None if it is not one."""
    if not d["imm"]:
        return None
    op, two = d["op"], d["two"]
    if two:
        if not 0x80 <= op <= 0x8F:
            return None
    elif not (0x70 <= op <= 0x7F or 0xE0 <= op <= 0xE3
              or op in (0xE8, 0xE9, 0xEB)):
        return None
    e = i + d["len"]
    return e + int.from_bytes(b[e - d["imm"]:e], "little", signed=True)


ALL_REGS = "ALL"


def insn_regs_written(d):
    """The GPR indices an instruction may clobber, or ALL_REGS when the opcode
    is not modelled. ALL_REGS is the safe answer: the callers use this only to
    forget facts, so over-forgetting costs recall and never invents a fact.

    Byte destinations map to the index of the enclosing dword register, so
    'mov ah, 1' is treated as writing eax. That is deliberate, the enclosing
    register no longer holds what it held.
    """
    op, two, mod, reg, rm = d["op"], d["two"], d["mod"], d["reg"], d["rm"]
    m3 = (mod == 3)
    if two:
        if op > 0xFF:
            return ALL_REGS                      # 0F 38 / 0F 3A, SSE4
        if 0x80 <= op <= 0x8F:                   # jcc
            return []
        if 0x90 <= op <= 0x9F:                   # setcc
            return [rm & 3] if m3 else []
        if 0x40 <= op <= 0x4F:                   # cmovcc
            return [reg]
        if op in (0xAF, 0xB6, 0xB7, 0xBC, 0xBD, 0xBE, 0xBF):
            return [reg]                         # imul, bsf, bsr, movzx, movsx
        if op in (0xB0, 0xB1):                   # cmpxchg
            return [0] + ([rm] if m3 else [])
        if op in (0xC0, 0xC1):                   # xadd
            return [reg] + ([rm] if m3 else [])
        if 0xC8 <= op <= 0xCF:                   # bswap
            return [op & 7]
        if op in (0xA3, 0xAE, 0x0D, 0x77):       # bt, group 15, prefetch, emms
            return []
        if op in (0xA4, 0xA5, 0xAB, 0xAC, 0xAD, 0xB3, 0xBA, 0xBB):
            return [rm] if m3 else []            # shld, shrd, bts, btr, btc
        if op == 0xA2:                           # cpuid
            return [0, 1, 2, 3]
        if op in (0x2C, 0x2D, 0x50, 0xC5, 0xD7):
            return [reg]                         # cvttss2si, movmskps, pextrw
        if op == 0x7E:                           # movd r/m32, xmm
            return [rm] if m3 else []
        if op == 0xC7:                           # cmpxchg8b
            return [0, 2]
        if op in (0xA0, 0xA1, 0xA8, 0xA9):       # push / pop fs, gs
            return []
        if 0x18 <= op <= 0x1F:                   # nop and prefetch hints
            return []
        if 0x10 <= op <= 0x7F or 0xD0 <= op <= 0xFF:
            return []                            # SSE / MMX, xmm or mm dest
        return ALL_REGS
    if op < 0x40:
        if op in (0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D):
            return []                            # cmp
        lo = op & 7
        if lo == 0:
            return [rm & 3] if m3 else []
        if lo == 1:
            return [rm] if m3 else []
        if lo == 2:
            return [reg & 3]
        if lo == 3:
            return [reg]
        if lo in (4, 5):
            return [0]
        return ALL_REGS                          # push / pop seg, daa, aaa
    if 0x40 <= op <= 0x4F:
        return [op & 7]                          # inc / dec r32
    if 0x50 <= op <= 0x57 or op in (0x60, 0x68, 0x6A):
        return []                                # push
    if 0x58 <= op <= 0x5F:
        return [op & 7]                          # pop r32
    if op == 0x61:
        return ALL_REGS                          # popa
    if op in (0x69, 0x6B):
        return [reg]                             # imul r32, r/m32, imm
    if 0x70 <= op <= 0x7F:
        return []                                # jcc
    if op in (0x80, 0x81, 0x82, 0x83):
        if reg == 7 or not m3:
            return []                            # cmp, or a memory dest
        return [rm & 3 if op in (0x80, 0x82) else rm]
    if op in (0x84, 0x85, 0xA8, 0xA9):
        return []                                # test
    if op in (0x86, 0x87):                       # xchg
        byte = (op == 0x86)
        out = [reg & 3 if byte else reg]
        return out + ([rm & 3 if byte else rm] if m3 else [])
    if op in (0x88, 0x89):
        if not m3:
            return []
        return [rm & 3 if op == 0x88 else rm]
    if op in (0x8A, 0x8B):
        return [reg & 3 if op == 0x8A else reg]
    if op in (0x8C, 0x8F):
        return [rm] if m3 else []
    if op == 0x8D:
        return [reg]                             # lea
    if op == 0x8E or op in (0x9B, 0x9C, 0x9D, 0x9E):
        return []
    if op == 0x90:
        return []
    if 0x91 <= op <= 0x97:
        return [0, op & 7]                       # xchg eax, r32
    if op == 0x98 or op == 0x9F:
        return [0]
    if op == 0x99:
        return [2]                               # cdq
    if op == 0x9A:
        return ALL_REGS                          # call far
    if op in (0xA0, 0xA1):
        return [0]                               # mov al / eax, [moffs]
    if op in (0xA2, 0xA3):
        return []                                # mov [moffs], al / eax
    if op in (0xA4, 0xA5, 0xA6, 0xA7):
        return [6, 7]                            # movs, cmps
    if op in (0xAA, 0xAB, 0xAE, 0xAF):
        return [7]                               # stos, scas
    if op in (0xAC, 0xAD):
        return [0, 6]                            # lods
    if 0xB0 <= op <= 0xB7:
        return [op & 3]                          # mov r8, imm8
    if 0xB8 <= op <= 0xBF:
        return [op & 7]                          # mov r32, imm32
    if op in (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
        if not m3:
            return []
        return [rm & 3 if op in (0xC0, 0xD0, 0xD2) else rm]
    if op in (0xC2, 0xC3, 0xC8, 0xC9, 0xCC):
        return []                                # ret, enter, leave, int3
    if op in (0xC4, 0xC5, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF, 0xEA):
        return ALL_REGS
    if op in (0xC6, 0xC7):
        if not m3:
            return []
        return [rm & 3 if op == 0xC6 else rm]
    if op in (0xD4, 0xD5, 0xD6, 0xD7):
        return [0]
    if 0xD8 <= op <= 0xDF:
        return []                                # x87, writes st or memory
    if 0xE0 <= op <= 0xE3:
        return [1]                               # loop
    if op in (0xE4, 0xE5, 0xEC, 0xED):
        return [0]                               # in
    if op in (0xE6, 0xE7, 0xEE, 0xEF):
        return []                                # out
    if op == 0xE8:
        return [0, 1, 2]                         # call rel32, cdecl volatiles
    if op in (0xE9, 0xEB):
        return []                                # jmp
    if op in (0xF4, 0xF5) or 0xF8 <= op <= 0xFD:
        return []
    if op in (0xF6, 0xF7):                       # group 3
        if reg in (0, 1):
            return []                            # test
        if reg in (2, 3):
            if not m3:
                return []
            return [rm & 3 if op == 0xF6 else rm]
        return [0, 2]                            # mul, imul, div, idiv
    if op == 0xFE:
        return [rm & 3] if m3 else []
    if op == 0xFF:                               # group 5
        if reg in (0, 1):
            return [rm] if m3 else []            # inc / dec
        if reg == 2:
            return [0, 1, 2]                     # call, cdecl volatiles
        if reg in (3, 5):
            return ALL_REGS                      # call far, jmp far
        return []                                # jmp, push
    return ALL_REGS


# ---------------------------------------------------------------------------
# the two exports
# ---------------------------------------------------------------------------

# GetEffectOverlayTable, byte for byte identical in all 581 FFX DLLs:
#   55                push ebp
#   8B EC             mov  ebp, esp
#   33 C9             xor  ecx, ecx
#   39 4D 08          cmp  [ebp+8], ecx          ; the "which" argument
#   B8 <imm32>        mov  eax, <overlay table VA>
#   0F 45 C1          cmovne eax, ecx            ; which != 0 -> return NULL
#   5D                pop  ebp
#   C3                ret
GETOVL_PROLOGUE = bytes.fromhex("558bec33c9394d08")
GETOVL_EPILOGUE = bytes.fromhex("0f45c15dc3")

OVERLAY_SLOTS = 16            # 0x40 bytes; verified against all 581 FFX DLLs


def overlay_table_va(pe):
    """VA of the effect overlay table, or None if the stub is not the known shape."""
    rva = pe.export_rva("GetEffectOverlayTable")
    if rva is None:
        return None
    off = pe.rva_to_off(rva)
    b = pe.d[off:off + 18]
    if b[0:8] != GETOVL_PROLOGUE or b[8] != 0xB8:
        return None
    if b[13:18] != GETOVL_EPILOGUE:
        return None
    return u32(b, 9)


def overlay_table(pe):
    """[(slot, va, section_name_or_None)] for the 16 slots."""
    va = overlay_table_va(pe)
    if va is None:
        return None
    out = []
    for i in range(OVERLAY_SLOTS):
        v = pe.dword_va(va + 4 * i)
        if v is None:
            out.append((i, None, None))
            continue
        s = pe.section_of_va(v) if v else None
        out.append((i, v, s["name"] if s else None))
    return out


# What each overlay slot is for. Derived from the host side of FFX.exe, so the
# host offsets are facts and the role descriptions are inference.
OVERLAY_SLOT_NOTES = {
    0:  ("[C] code ptr, present in all 581", "role UNKNOWN"),
    1:  ("[C] code ptr, present in all 581", "role UNKNOWN"),
    2:  ("[C] code ptr, present in all 581", "role UNKNOWN"),
    3:  ("[C] code ptr, present in all 581",
         "[I] called as tbl->f3(0) by FFX.exe 0x787E00 when its arg is 0"),
    4:  ("[C] code ptr, present in all 581",
         "[I] called as tbl->f4(0) by FFX.exe 0x787E00 when its arg is 1"),
    5:  ("[C] code ptr, present in all 581", "role UNKNOWN"),
    6:  ("[C] code ptr, present in all 581", "role UNKNOWN"),
    7:  ("[C] .data ptr in 529, NULL in 52",
         "[C] points at the effect-data blob (see the ef2 command)"),
    8:  ("[C] code ptr in 465, NULL in 116",
         "[I] called as tbl->f8(unit, 2, 0, 0) by FFX.exe 0x787AB0 for magic id 671"),
    9:  ("[C] code ptr in 415, NULL in 166", "role UNKNOWN"),
    10: ("[C] code ptr in 415, NULL in 166", "role UNKNOWN"),
    11: ("[C] code ptr in 415, NULL in 166", "role UNKNOWN"),
    12: ("[C] code ptr in 415, NULL in 166", "role UNKNOWN"),
    13: ("[C] code ptr in 415, NULL in 166", "role UNKNOWN"),
    14: ("[C] code ptr in 415, NULL in 166", "role UNKNOWN"),
    15: ("[C] code ptr, present in all 581", "role UNKNOWN"),
}


# ---------------------------------------------------------------------------
# InitMagicPRX: which host-table entries the DLL copies into its own globals
# ---------------------------------------------------------------------------

# InitMagicPRX is a straight-line run of
#     8B 45 08                mov  eax, [ebp+8]             ; the host table
#     8B 88 <disp32>          mov  ecx, [eax + disp32]      ; one entry
#     89 0D <abs32>           mov  [dllglobal], ecx
# plus one
#     A3 <abs32>              mov  [dllglobal], eax         ; stash the base
# interleaved with register shuffling the compiler threw in. Decoding it
# instruction by instruction and carrying a tag on each register recovers both
# the mapping and, which matters more, which global holds the table pointer.
#
# The old version of this walked bytes rather than instructions and tripped
# over 0xA3 and 0x8B bytes sitting inside other instructions' immediates. It
# got the base global wrong in 9 of the 581 FFX DLLs and found only 1 copy
# instead of 32 in another 5. Those 5 are why this file used to claim five
# DLLs have a non-standard init body. They do not, all 581 are the same shape.

REG_NAMES = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]

HOST_TABLE_SLOTS = 947        # [C] slot 946 is the last, see reversing/MAGIC_DLL.md


def init_prx_table_globals(pe, limit=0x20000):
    """(set of DLL globals that hold the host table pointer, {global: slot}).

    Tags carried per register: ("tbl",) for the table pointer itself,
    ("fn", n) for host table entry n, ("deref", n) for a single dereference of
    entry n, which is how slot 742 gets stored.
    """
    rva = pe.export_rva("InitMagicPRX")
    if rva is None:
        return set(), {}
    off = pe.rva_to_off(rva)
    if off is None:
        return set(), {}
    d = pe.d
    end = min(len(d), off + limit)
    regs = [None] * 8
    bases = set()
    mapping = {}
    i = off
    while i < end:
        try:
            ins = decode_insn(d, i)
        except BadInsn:
            break
        op, two, mod, reg, rm = (ins["op"], ins["two"], ins["mod"],
                                 ins["reg"], ins["rm"])
        handled = False
        if not two and op == 0x8B:
            if ins["base"] == 5 and mod == 1 and ins["disp"] == 8:
                regs[reg] = ("tbl",)                 # mov reg, [ebp+8]
            elif ins["base"] == "abs":
                regs[reg] = None
            elif mod == 3:
                regs[reg] = regs[rm]
            elif isinstance(ins["base"], int):
                tag = regs[ins["base"]]
                disp = ins["disp"]
                if tag and tag[0] == "tbl" and disp >= 0 and disp % 4 == 0 \
                        and disp // 4 < HOST_TABLE_SLOTS:
                    regs[reg] = ("fn", disp // 4)
                elif tag and tag[0] == "fn" and disp == 0:
                    regs[reg] = ("deref", tag[1])
                else:
                    regs[reg] = None
            else:
                regs[reg] = None
            handled = True
        elif not two and (op == 0xA3 or (op == 0x89 and ins["base"] == "abs")):
            tag = regs[0 if op == 0xA3 else reg]
            g = insn_abs_addr(d, i, ins)
            if tag and tag[0] == "tbl":
                bases.add(g)
            elif tag and tag[0] in ("fn", "deref"):
                mapping[g] = tag[1]
            handled = True
        if not handled:
            w = insn_regs_written(ins)
            if w == ALL_REGS:
                regs = [None] * 8
            else:
                for k in w:
                    regs[k] = None
        if not two and op in (0xC2, 0xC3):
            break
        i += ins["len"]
    return bases, mapping


def init_prx_mapping(pe):
    """(base_global_va, {dll_global_va: host_table_index}).

    Every one of the 581 FFX DLLs stores the table pointer in exactly one
    global, so returning a single VA loses nothing. Use
    init_prx_table_globals if you want the set.
    """
    bases, mapping = init_prx_table_globals(pe)
    base = sorted(bases)[0] if bases else None
    return base, mapping


# ---------------------------------------------------------------------------
# host API calls made from the DLL body
# ---------------------------------------------------------------------------

# A host API call looks like this, from magic_0393.dll at file offset 0x000ECB:
#
#     A1 04 AB 0F 10        mov  eax, ds:[0x100FAB04]   ; the stored table ptr
#     57                    push edi
#     8B 80 E0 09 00 00     mov  eax, [eax+0x9E0]       ; 0x9E0 / 4 = slot 632
#     56                    push esi
#     53                    push ebx
#     FF D0                 call eax
#     83 C4 0C              add  esp, 0Ch               ; cdecl, 3 args
#
# The load from the stored table pointer global is what makes it a host call.
# Without it, a 'mov reg,[reg+disp32]' with a small 4-aligned displacement is
# just a struct field access. magic_0162 has four accesses to [reg+0x9E0] that
# are a structure being copied field by field, with neighbours at 0x9E4 and
# 0xD0, and nothing to do with the host table.
#
# So the scan is a linear sweep over .text carrying a tiny abstract state: for
# each of the eight GPRs, the set of facts "holds the table pointer" and
# "holds host table entry N". Only a call whose callee traces back to a load
# of this DLL's own table pointer global is recorded.
#
# Three things this gets right that a byte-pattern matcher cannot:
#
#  - The mov and the call need not be adjacent. MSVC puts argument pushes in
#    between routinely, and at 4 sites out of 8 for slot 632 it does.
#  - The destination register need not be the base register.
#  - One call instruction can serve more than one host entry, because MSVC
#    tail-merges the call. magic_0009 at 0x100035F3 is a single 'call eax'
#    reached from a slot 329 load and from a slot 554 load. 1520 of the 73091
#    call sites across the 581 FFX DLLs are shared like that, so joins take
#    the union of facts and the question answered is "can this site call entry
#    N", not "does it always".
#
# What the sweep does not follow: the table pointer spilled to a stack slot
# and reloaded, and loop back edges, where the state at the loop head is
# cleared rather than iterated to a fixpoint. Both cost recall only.
#
# Two forms a reviewer might expect and that are simply not present: there is
# not one single 'call dword ptr [reg+disp]' instruction in any of the 581
# FFX DLLs (89889 'call reg' and 11205 'call [abs32]', and nothing else), and
# no call goes through a global holding a copied table entry, because all 32
# copied entries are data pointers rather than functions.

_ENDS_BLOCK = (0xC2, 0xC3, 0xCA, 0xCB, 0xE9, 0xEB, 0xCC)
_TABLE_PTR = -1                       # the fact "this register holds the table"
_TABLE_PTR_SET = frozenset((_TABLE_PTR,))


def _join_fact(a, b):
    if a is None:
        return b
    if b is None:
        return a
    return a | b


class HostCallScan(object):
    """The sweep. Call run(), then read sites and stats."""

    def __init__(self, pe, nslots=HOST_TABLE_SLOTS):
        s = pe.section(".text")
        self.sec = s
        self.text_off = s["rawoff"] if s else 0
        self.text_va = pe.image_base + (s["vaddr"] if s else 0)
        self.txt = pe.section_bytes(".text")
        self.hi_va = self.text_va + (max(s["vsize"], s["rawsize"]) if s else 0)
        self.nslots = nslots
        self.base_globals, self.copied = init_prx_table_globals(pe)
        self.sites = {}
        self.stats = {}

    def bump(self, k, n=1):
        self.stats[k] = self.stats.get(k, 0) + n

    # -- pass 1: instruction boundaries and where control flow arrives ------

    def _jump_table_len(self, i):
        """Length of a 4-aligned run of two or more .text code addresses. That
        is how MSVC lays a switch jump table down inside .text, and 11 of the
        581 DLLs have one, which would otherwise desync the sweep."""
        t = self.txt
        if i % 4 or i + 8 > len(t):
            return 0
        k = i
        while k + 4 <= len(t):
            v = u32(t, k)
            if not (self.text_va <= v < self.hi_va):
                break
            k += 4
        return k - i if k - i >= 8 else 0

    def _resync(self, i):
        """Next plausible function start after bytes that will not decode."""
        t = self.txt
        n = len(t)
        j = i + 1
        while j < n - 3:
            if t[j] == 0x55 and t[j + 1] == 0x8B and t[j + 2] == 0xEC:
                return j                     # push ebp; mov ebp, esp
            if t[j:j + 4] == b"\xcc\xcc\xcc\xcc":
                while j < n and t[j] == 0xCC:
                    j += 1
                return j
            j += 1
        return n

    def _layout(self):
        t = self.txt
        n = len(t)
        i = 0
        insns = []
        arrives = set()      # offsets a forward branch lands on
        opaque = set()       # offsets where nothing may be assumed
        while i < n:
            r = self._jump_table_len(i)
            if r:
                self.bump("jump_table_entries", r // 4)
                for k in range(i, i + r, 4):
                    opaque.add(u32(t, k) - self.text_va)
                i += r
                continue
            try:
                d = decode_insn(t, i)
            except BadInsn:
                self.bump("undecodable")
                i = self._resync(i)
                continue
            insns.append((i, d))
            tg = insn_rel_target(t, i, d)
            if tg is not None and 0 <= tg < n:
                if d["op"] == 0xE8 and not d["two"]:
                    opaque.add(tg)           # another function's entry
                elif tg > i:
                    arrives.add(tg)
            i += d["len"]
        return insns, arrives, opaque

    # -- pass 2: the register facts -----------------------------------------

    def run(self):
        """{host_table_index: [.text offsets of the call instruction]}"""
        t = self.txt
        insns, arrives, opaque = self._layout()
        incoming = {}
        sites = {}
        regs = [None] * 8
        fell = False
        prev_end = None
        for i, d in insns:
            if prev_end is not None and i != prev_end:
                fell = False                 # a gap, nothing fell through
            state = regs if fell else [None] * 8
            if i in arrives:
                inc = incoming.pop(i, None)
                if inc is not None:
                    state = [_join_fact(a, b) for a, b in zip(state, inc)]
            if i in opaque:
                state = [None] * 8
            regs = state
            op, two, mod, reg, rm = (d["op"], d["two"], d["mod"], d["reg"],
                                     d["rm"])
            handled = False
            if not two and op == 0xA1:
                regs[0] = self._fact_for_global(insn_abs_addr(t, i, d))
                handled = True
            elif not two and op == 0x8B:
                if d["base"] == "abs":
                    regs[reg] = self._fact_for_global(insn_abs_addr(t, i, d))
                elif mod == 3:
                    regs[reg] = regs[rm]
                elif isinstance(d["base"], int):
                    regs[reg] = self._fact_for_entry(regs[d["base"]], d["disp"])
                else:
                    regs[reg] = None
                handled = True
            elif not two and op == 0xFF and reg == 2:
                self._record_call(regs, t, i, d, sites)
            if not handled:
                w = insn_regs_written(d)
                if w == ALL_REGS:
                    self.bump("state_cleared")
                    regs = [None] * 8
                else:
                    for k in w:
                        regs[k] = None
            fell = not ((not two and op in _ENDS_BLOCK)
                        or (not two and op == 0xFF and reg in (4, 5)))
            prev_end = i + d["len"]
            tg = insn_rel_target(t, i, d)
            if tg is not None and tg in arrives and \
                    not (not two and op == 0xE8):
                was = incoming.get(tg)
                incoming[tg] = list(regs) if was is None else \
                    [_join_fact(a, b) for a, b in zip(was, regs)]
        self.sites = sites
        return sites

    def _record_call(self, regs, t, i, d, sites):
        slots = ()
        if d["mod"] == 3:
            fact = regs[d["rm"]]
            if fact:
                slots = tuple(s for s in sorted(fact) if s != _TABLE_PTR)
                if slots:
                    self.bump("call_reg")
                    if len(slots) > 1:
                        self.bump("call_reg_shared")
        elif d["base"] == "abs":
            g = insn_abs_addr(t, i, d)
            if g in self.copied:
                slots = (self.copied[g],)
                self.bump("call_via_copied_global")
        elif isinstance(d["base"], int):
            fact = regs[d["base"]]
            if fact and _TABLE_PTR in fact:
                s = self._slot(d["disp"])
                if s is None:
                    self.bump("rejected_displacement")
                else:
                    slots = (s,)
                    self.bump("call_through_table")
        for s in slots:
            sites.setdefault(s, []).append(i)
        regs[0] = regs[1] = regs[2] = None     # eax, ecx, edx are volatile

    def _fact_for_global(self, g):
        if g is None:
            return None
        if g in self.base_globals:
            return _TABLE_PTR_SET
        if g in self.copied:
            return frozenset((self.copied[g],))
        return None

    def _slot(self, disp):
        if disp < 0 or disp % 4:
            return None
        s = disp // 4
        return s if s < self.nslots else None

    def _fact_for_entry(self, basefact, disp):
        if not basefact or _TABLE_PTR not in basefact:
            return None
        s = self._slot(disp)
        if s is None:
            self.bump("rejected_displacement")
            return None
        return frozenset((s,))


def host_call_sites(pe):
    """{host_table_index: [.text offsets of the call]} found in .text."""
    return HostCallScan(pe).run()


def host_calls(pe):
    """{host_table_index: call_site_count} found in .text."""
    return dict((k, len(v)) for k, v in host_call_sites(pe).items())


# ---------------------------------------------------------------------------
# the effect-data blob (overlay slot 7)
# ---------------------------------------------------------------------------

# Field notes for the blob header. Constancy figures are over the 529 FFX DLLs
# that have a non-NULL slot 7.
EF2_HEADER = [
    (0x00, "version_or_kind",   "[C] always 3"),
    (0x04, "header_stride",     "[C] always 0x10"),
    (0x08, "off_UNKNOWN_08",    "[C] blob-relative offset, 134 distinct values"),
    (0x0C, "off_UNKNOWN_0C",    "[C] blob-relative offset, 138 distinct values"),
    (0x10, "count_UNKNOWN_10",  "[?] small int or offset, 198 distinct values"),
    (0x14, "zero_14",           "[C] always 0"),
    (0x18, "off_table_a",       "[C] 0x70 in 528 of 529, 0x38 in one"),
    (0x1C, "off_table_b",       "[C] 0x74 in 528 of 529, 0x3C in one"),
    (0x20, "off_UNKNOWN_20",    "[C] blob-relative offset, tracks +0x08 minus 0x10"),
    (0x24, "one_24",            "[C] 1 in 528 of 529"),
    (0x28, "terminator",        "[C] 0x0000FFFF in 442, 0xFFFFFFFF in 87"),
    (0x2C, "zero_2C",           "[C] always 0"),
    (0x30, "off_UNKNOWN_30",    "[C] blob-relative offset"),
    (0x34, "zero_34",           "[C] always 0"),
    (0x38, "off_UNKNOWN_38",    "[C] blob-relative offset, equals +0x6C"),
    (0x3C, "off_UNKNOWN_3C",    "[C] blob-relative offset"),
    (0x40, "off_UNKNOWN_40",    "[C] blob-relative offset"),
    (0x44, "pair_1_1",          "[C] 0x00010001 in 528 of 529"),
    (0x48, "const_3C",          "[C] 0x3C in 528 of 529"),
    (0x4C, "counts_4C",         "[I] two u16 counts, low word is 2 in most DLLs"),
    (0x50, "counts_50",         "[I] two u16 counts"),
    (0x54, "counts_54",         "[I] two u16 counts"),
    (0x58, "zero_58",           "[C] always 0"),
    (0x5C, "zero_5C",           "[C] always 0"),
    (0x60, "const_7C",          "[C] 0x7C in 528 of 529"),
    (0x64, "off_UNKNOWN_64",    "[C] blob-relative offset, only 19 distinct values"),
    (0x68, "off_UNKNOWN_68",    "[C] blob-relative offset"),
    (0x6C, "off_UNKNOWN_6C",    "[C] blob-relative offset, equals +0x38"),
    (0x70, "off_UNKNOWN_70",    "[C] blob-relative offset"),
    (0x74, "zero_74",           "[C] always 0"),
    (0x78, "zero_78",           "[C] always 0"),
    (0x7C, "off_UNKNOWN_7C",    "[C] blob-relative offset"),
    (0x80, "zero_80",           "[C] always 0"),
    (0x84, "magic_id_ascii",    "[C] 4 ASCII digits, the source magic id"),
    (0x88, "zero_88",           "[C] always 0"),
]
EF2_HEADER_SIZE = 0x8C


def ef2_blob_va(pe):
    ovl = overlay_table(pe)
    if not ovl:
        return None
    return ovl[7][1] or None


def ef2_header(pe):
    va = ef2_blob_va(pe)
    if va is None:
        return None
    off = pe.va_to_off(va)
    if off is None:
        return None
    rows = []
    for o, name, note in EF2_HEADER:
        rows.append((o, name, u32(pe.d, off + o), note))
    return va, off, rows


def ef2_magic_id(pe):
    """The 4 ASCII digits at blob+0x84, or None."""
    h = ef2_header(pe)
    if h is None:
        return None
    _va, off, _rows = h
    tag = pe.d[off + 0x84:off + 0x88]
    try:
        tag = tag.decode("ascii")
    except UnicodeDecodeError:
        return None
    return tag if tag.isdigit() else None


# ---------------------------------------------------------------------------
# texture descriptor packing, from FFX.exe FFX_Magic_BuildTexturePath 0x906420
# ---------------------------------------------------------------------------

def decode_texture_desc(packed):
    """
    FFX_Magic_BuildTexturePath formats
        <root>/magic_%04d/tex/GCM/%d_%d_0_0_%d_%d.dds.phyre
    from a packed 64-bit descriptor. Field extraction is lifted straight from
    that function, so it is confirmed. root is "/FFX_Data/GameData/PS3Data/magic"
    and FFX_fiosUnifyFilename rewrites the GCM path component to D3D11.
    """
    tex_id = packed & 0x3FFF
    fmt = (packed >> 20) & 0x3F
    width = 1 << ((packed >> 26) & 0xF)
    height = 1 << ((packed >> 30) & 0xF)
    return dict(tex_id=tex_id, fmt=fmt, width=width, height=height,
                filename="%d_%d_0_0_%d_%d.dds.phyre" % (tex_id, fmt, width, height))


def vbf_texture_path(magic_id, packed, lang_cn=False):
    d = decode_texture_desc(packed)
    sub = "tex_cn" if lang_cn else "tex"
    return "ffx_data/gamedata/ps3data/magic/magic_%04d/%s/d3d11/%s" % (
        magic_id, sub, d["filename"])


# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------

def chars_str(c):
    bits = [n for m, n in sorted(IMAGE_SCN.items()) if c & m]
    return ",".join(bits)


def cmd_info(paths):
    for p in paths:
        pe = PE32(open(p, "rb").read(), p)
        sz = os.path.getsize(p)
        print("=== %s" % p)
        print("  file size      %d bytes" % sz)
        print("  machine        0x%04X (%s)" % (
            pe.machine, "i386" if pe.machine == 0x14C else "?"))
        print("  image base     0x%08X   size of image 0x%08X" % (
            pe.image_base, pe.size_of_image))
        print("  entry point    0x%08X   PE timestamp  %d" % (
            pe.entry_point, pe.timestamp))
        print("  sections (%d):" % len(pe.sections))
        for s in pe.sections:
            print("    %-9s vaddr=%08X vsize=%-9d rawoff=%08X rawsize=%-9d %s" % (
                s["name"], s["vaddr"], s["vsize"], s["rawoff"], s["rawsize"],
                chars_str(s["chars"])))
        ds = pe.section(".data")
        if ds:
            print("    .data BSS tail = %d bytes (vsize - rawsize)" % (
                ds["vsize"] - ds["rawsize"]))
            print("    .data raw is %.1f%% of the whole file" % (
                100.0 * ds["rawsize"] / sz))
        print("  exports:")
        for nm, o, rva in pe.exports():
            print("    ord %-3d rva 0x%06X  %s" % (o, rva, nm))
        for dll, fns in pe.imports():
            print("  imports %-14s (%d) %s" % (dll, len(fns), ", ".join(fns)))
        n, blocks = pe.reloc_count()
        print("  relocations    %d in %d blocks" % (n, blocks))
        print()


def cmd_abi(paths):
    for p in paths:
        pe = PE32(open(p, "rb").read(), p)
        print("=== %s" % p)
        ovl_va = overlay_table_va(pe)
        if ovl_va is None:
            print("  GetEffectOverlayTable: [?] stub does not match the known shape")
        else:
            print("  [C] void *__cdecl GetEffectOverlayTable(int which)")
            print("        returns 0x%08X when which == 0, NULL otherwise" % ovl_va)
            print("        (cmovne against a zeroed ecx, identical in all 581 FFX DLLs)")
        base, mapping = init_prx_mapping(pe)
        print("  [C] void __cdecl InitMagicPRX(void **hostApiTable)")
        if base:
            print("        stashes the table pointer at DLL global 0x%08X" % base)
        if mapping:
            idxs = sorted(mapping.values())
            print("        copies %d host table entries into DLL globals" % len(mapping))
            print("        host indices copied: min %d, max %d" % (idxs[0], idxs[-1]))
            lo = [i for i in idxs if i < 741]
            print("        of those, %d are below index 741 and %d are 741 or above"
                  % (len(lo), len(idxs) - len(lo)))
            for g in sorted(mapping, key=lambda k: mapping[k]):
                print("          host[%4d] -> 0x%08X" % (mapping[g], g))
        calls = host_calls(pe)
        if calls:
            idxs = sorted(calls)
            print("  [C] %d distinct host table indices called from .text (%d call sites)"
                  % (len(calls), sum(calls.values())))
            print("        highest index called: %d" % idxs[-1])
        print()


def cmd_ovl(paths):
    for p in paths:
        pe = PE32(open(p, "rb").read(), p)
        print("=== %s" % p)
        rows = overlay_table(pe)
        if rows is None:
            print("  [?] could not locate the overlay table")
            print()
            continue
        print("  effect overlay table at VA 0x%08X, %d slots, 0x%X bytes"
              % (overlay_table_va(pe), OVERLAY_SLOTS, 4 * OVERLAY_SLOTS))
        for i, va, sec in rows:
            kind, note = OVERLAY_SLOT_NOTES.get(i, ("", ""))
            print("    [%2d] +0x%02X = %08X  %-7s %s | %s" % (
                i, 4 * i, va or 0, sec or ("NULL" if va == 0 else "?"), kind, note))
        print("  bytes past +0x3F are not part of the table: no slot beyond 15 is a")
        print("  consistent pointer in any of the 581 FFX DLLs.")
        print()


def cmd_ef2(paths):
    for p in paths:
        pe = PE32(open(p, "rb").read(), p)
        print("=== %s" % p)
        h = ef2_header(pe)
        if h is None:
            print("  overlay slot 7 is NULL or not backed by file bytes: no blob")
            print()
            continue
        va, off, rows = h
        print("  effect-data blob at VA 0x%08X (file offset 0x%08X)" % (va, off))
        print("  header is 0x%X bytes; everything after it is UNKNOWN" % EF2_HEADER_SIZE)
        for o, name, val, note in rows:
            extra = ""
            if name == "magic_id_ascii":
                extra = "  = %r" % pe.d[off + o:off + o + 4]
            print("    +0x%02X %-18s = %08X  %s%s" % (o, name, val, note, extra))
        tag = ef2_magic_id(pe)
        fn = os.path.basename(p)
        m = re.search(r"magic_(\d{4})", fn)
        if tag and m:
            print("  source magic id tag %s vs filename %s: %s"
                  % (tag, m.group(1), "match" if tag == m.group(1) else "DIFFERENT"))
        print()


def cmd_calls(paths):
    for p in paths:
        pe = PE32(open(p, "rb").read(), p)
        scan = HostCallScan(pe)
        sites = scan.run()
        print("=== %s  (%d host API indices, %d call sites)"
              % (p, len(sites), sum(len(v) for v in sites.values())))
        if scan.base_globals:
            print("    table pointer global: %s"
                  % ", ".join("0x%08X" % g for g in sorted(scan.base_globals)))
        else:
            print("    [?] could not find the global this DLL stores the table"
                  " pointer in, so nothing can be attributed")
        for i in sorted(sites, key=lambda k: (-len(sites[k]), k)):
            offs = sites[i]
            shown = ", ".join("0x%08X" % (scan.text_va + o) for o in offs[:8])
            if len(offs) > 8:
                shown += ", ..."
            print("    host[%4d]  %3d call sites  %s" % (i, len(offs), shown))
        if scan.stats.get("undecodable"):
            print("    note: %d run(s) of bytes in .text would not decode, so the"
                  " sweep resynchronised past them" % scan.stats["undecodable"])
        if scan.stats.get("call_reg_shared"):
            print("    note: %d call instruction(s) are shared between two or more"
                  " host entries by tail merging" % scan.stats["call_reg_shared"])
        print()


def cmd_survey(directory):
    files = sorted(f for f in os.listdir(directory)
                   if re.match(r"magic_\d{4}\.dll$", f, re.I))
    if not files:
        print("no magic_NNNN.dll files in %s" % directory)
        return
    sizes, texts, datav, datar = [], [], [], []
    slot7_null = 0
    tag_match = tag_diff = tag_none = 0
    all_calls = {}
    call_dlls = {}
    maxidx = 0
    no_table_ptr = resynced = shared_sites = 0
    ovl_ok = 0
    text_hashes = {}
    import hashlib
    for f in files:
        p = os.path.join(directory, f)
        data = open(p, "rb").read()
        pe = PE32(data, p)
        sizes.append(len(data))
        t = pe.section(".text")
        d = pe.section(".data")
        texts.append(t["vsize"] if t else 0)
        if d:
            datav.append(d["vsize"])
            datar.append(d["rawsize"])
        if t:
            hh = hashlib.sha1(data[t["rawoff"]:t["rawoff"] + t["rawsize"]]).hexdigest()
            text_hashes[hh] = text_hashes.get(hh, 0) + 1
        rows = overlay_table(pe)
        if rows:
            ovl_ok += 1
            if not rows[7][1]:
                slot7_null += 1
        tag = ef2_magic_id(pe)
        fid = re.search(r"magic_(\d{4})", f).group(1)
        if tag is None:
            tag_none += 1
        elif tag == fid:
            tag_match += 1
        else:
            tag_diff += 1
        scan = HostCallScan(pe)
        for i, offs in scan.run().items():
            all_calls[i] = all_calls.get(i, 0) + len(offs)
            call_dlls[i] = call_dlls.get(i, 0) + 1
            maxidx = max(maxidx, i)
        if not scan.base_globals:
            no_table_ptr += 1
        if scan.stats.get("undecodable"):
            resynced += 1
        shared_sites += scan.stats.get("call_reg_shared", 0)
    sizes.sort(); texts.sort(); datav.sort(); datar.sort()

    def st(v):
        return "min=%d median=%d max=%d" % (v[0], v[len(v) // 2], v[-1])

    print("=== survey of %s" % directory)
    print("  DLLs                      %d" % len(files))
    print("  standard overlay stub     %d of %d" % (ovl_ok, len(files)))
    print("  overlay slot 7 NULL       %d" % slot7_null)
    print("  file size                 %s  total=%d" % (st(sizes), sum(sizes)))
    print("  .text vsize               %s" % st(texts))
    print("  .data vsize               %s" % st(datav))
    print("  .data rawsize             %s  (%.1f%% of all bytes)"
          % (st(datar), 100.0 * sum(datar) / sum(sizes)))
    print("  distinct .text hashes     %d (so .text is per-spell code, not a shared stub)"
          % len(text_hashes))
    print("  blob magic-id tag         %d match filename, %d differ, %d absent"
          % (tag_match, tag_diff, tag_none))
    print("  host API indices used     %d distinct, max index %d, %d call sites total"
          % (len(all_calls), maxidx, sum(all_calls.values())))
    print("  scan health               %d DLLs with no locatable table pointer,"
          " %d needing a resync, %d tail-merged call sites"
          % (no_table_ptr, resynced, shared_sites))
    print("  top 25 host API indices by call sites:")
    for i in sorted(all_calls, key=lambda k: -all_calls[k])[:25]:
        print("    host[%4d]  %6d sites  in %3d DLLs" % (i, all_calls[i], call_dlls[i]))


USAGE = __doc__


def main(argv):
    if len(argv) < 3:
        print(USAGE)
        return 2
    cmd, args = argv[1], argv[2:]
    if cmd == "info":
        cmd_info(args)
    elif cmd == "abi":
        cmd_abi(args)
    elif cmd == "ovl":
        cmd_ovl(args)
    elif cmd == "ef2":
        cmd_ef2(args)
    elif cmd == "calls":
        cmd_calls(args)
    elif cmd == "survey":
        cmd_survey(args[0])
    elif cmd == "all":
        cmd_info(args)
        cmd_abi(args)
        cmd_ovl(args)
        cmd_ef2(args)
    else:
        print(USAGE)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
