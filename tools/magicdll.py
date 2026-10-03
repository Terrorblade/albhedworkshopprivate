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
    python magicdll.py calls   <dll> [...]     host API table indices this DLL calls
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
#     8B 8x <disp32>          mov  reg, [eax + disp32]      ; eax = host table
#     89 0D <abs32>           mov  [dllglobal], reg
# plus one
#     A3 <abs32>              mov  [dllglobal], eax         ; stash the base
# so a byte-level scan recovers the mapping without a full disassembler.
RE_LOAD_FROM_TABLE = re.compile(rb"\x8b([\x80-\xbf])(....)", re.S)
RE_STORE_GLOBAL = re.compile(rb"\x89([\x05\x0d\x15\x1d\x25\x2d\x35\x3d])(....)", re.S)
RE_STORE_EAX = re.compile(rb"\xa3(....)", re.S)

REG_NAMES = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]


def _init_prx_body(pe, limit=0x4000):
    rva = pe.export_rva("InitMagicPRX")
    if rva is None:
        return None, None
    off = pe.rva_to_off(rva)
    body = pe.d[off:off + limit]
    end = body.find(b"\xc3")           # first ret; the function is straight-line
    if end >= 0:
        body = body[:end + 1]
    return body, pe.image_base + rva


def init_prx_mapping(pe):
    """
    Returns (base_global_va, {dll_global_va: host_table_index}).

    Walks InitMagicPRX byte by byte pairing each 'mov reg,[eax+disp]' with the
    'mov [global],reg' that follows it for the same register.
    """
    body, _va = _init_prx_body(pe)
    if body is None:
        return None, {}
    mapping = {}
    base_global = None
    pending = {}                       # reg index -> host table index
    i = 0
    n = len(body)
    while i < n:
        b = body[i]
        if b == 0x8B and i + 6 <= n and 0x80 <= body[i + 1] <= 0xBF:
            modrm = body[i + 1]
            reg = (modrm >> 3) & 7
            basereg = modrm & 7
            if basereg != 4:           # no SIB
                disp = u32(body, i + 2)
                if disp % 4 == 0 and disp < 0x10000:
                    pending[reg] = disp // 4
                i += 6
                continue
        if b == 0x89 and i + 6 <= n and (body[i + 1] & 0xC7) == 0x05:
            reg = (body[i + 1] >> 3) & 7
            g = u32(body, i + 2)
            if reg in pending:
                mapping[g] = pending.pop(reg)
            i += 6
            continue
        if b == 0xA3 and i + 5 <= n:
            # mov [abs32], eax. The first one in the function is the table
            # pointer itself, since eax still holds the raw argument there.
            # Later ones are ordinary entry copies that happened to use eax.
            if base_global is None:
                base_global = u32(body, i + 1)
            elif 0 in pending:
                mapping[u32(body, i + 1)] = pending.pop(0)
            i += 5
            continue
        i += 1
    return base_global, mapping


# ---------------------------------------------------------------------------
# host API calls made from the DLL body
# ---------------------------------------------------------------------------

# The DLL calls a host function as
#     A1 <base>          mov  eax, [table base global]
#     ...pushes...
#     8B 80 <disp32>     mov  eax, [eax + disp32]
#     FF D0              call eax
# so the pair 'mov reg,[reg+disp32]; call reg' with a matching register is a
# reliable fingerprint for "call host table entry disp/4".
RE_TABLE_CALL = re.compile(rb"\x8b([\x80-\xbf])(....)\xff([\xd0-\xd7])", re.S)


def host_calls(pe):
    """{host_table_index: call_site_count} found in .text."""
    out = {}
    txt = pe.section_bytes(".text")
    for m in RE_TABLE_CALL.finditer(txt):
        modrm = m.group(1)[0]
        reg = (modrm >> 3) & 7
        basereg = modrm & 7
        callreg = m.group(3)[0] & 7
        if basereg == 4 or reg != basereg or callreg != reg:
            continue
        disp = u32(m.group(2), 0)
        if disp >= 0x1000 or disp % 4:
            continue
        out[disp // 4] = out.get(disp // 4, 0) + 1
    return out


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
        calls = host_calls(pe)
        print("=== %s  (%d host API indices, %d call sites)"
              % (p, len(calls), sum(calls.values())))
        for i in sorted(calls, key=lambda k: -calls[k]):
            print("    host[%4d]  %d call sites" % (i, calls[i]))
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
        for i, c in host_calls(pe).items():
            all_calls[i] = all_calls.get(i, 0) + c
            call_dlls[i] = call_dlls.get(i, 0) + 1
            maxidx = max(maxidx, i)
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
