"""
Reader for the PS2-era .chr character containers in FINAL FANTASY X HD Remaster.

The format was recovered from FFX.exe, not guessed:

  FFX_Ch_RelocateChrBlob     0x8256F0  the top-level section walker, which is what pins the
                                       header down completely
  FFX_Ch_InitChrDataFromBlob 0x825EE0  copies header fields into the CHRDATA struct
  FFX_Ch_RelocateModelSection 0x827590 section 0's internal pointer fixups
  FFX_Ch_RelocateTexSection  0x83CBA0  section 1
  FFX_Mot_RelocateIdTable    0x8377A0  section 4
  FFX_Ch_LookupBonePoint     0x833A70  section 2 record layout
  FFX_Mot_SetByModeIndex     0x837D00  sections 5-8, via g_ffxMotModeToSetSlot {5,6,7,8}
  FFX_Ch_BuildSkeletonInstance 0x8277F0 section 0 joint table

Everything the reader prints is tagged CONFIRMED (a function in the exe reads that exact
field) or INFERRED (derived from offset arithmetic that holds across all 865 shipped files).

The files live in the VBF at
  ffx_ps2/ffx/master/jppc/chr/<cat>/<name>/mdl/<name>.chr
with <cat> in {pc, mon, npc, obj, skl, sum, wep}.

Usage:
    python chrfile.py list [pc]                 the .chr files in the archive
    python chrfile.py dump pc/c001              the full report for one character
    python chrfile.py dump <archive path>       ... or an explicit archive path
    python chrfile.py dump c001.chr --file      ... or a file on disk
    python chrfile.py verify                    parse all 865 and report anomalies

    import chrfile
    c = chrfile.ChrFile.from_archive("pc/c001")
    c.sections[2].records          # the bone-point table
"""

import argparse
import os
import struct
import sys

try:
    import vbf
except ImportError:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import vbf

CHR_ROOT = "ffx_ps2/ffx/master/jppc/chr/"

# The pad byte the tools wrote between sections. Not a terminator, just fill.
PAD_BYTE = 0x77

# Section index -> (short name, what it is, how sure we are).
#
# The indices are fixed: FFX_Ch_RelocateChrBlob switches on the loop counter, and
# FFX_Ch_InitChrDataFromBlob reads specific header offsets, so section N always means
# the same thing.
SECTIONS = {
    0: ("model",      "PS2 model + skeleton blob. CHRDATA.m_partTable (+0x04). Relocated "
                      "recursively by FFX_Ch_RelocateModelSection.", "CONFIRMED"),
    1: ("tex",        "PS2 texture / CLUT blob. CHRDATA.m_texData (+0x14), aux -> CHRDATA+0x18. "
                      "Relocated by FFX_Ch_RelocateTexSection.", "CONFIRMED"),
    2: ("bonepoints", "Bone-point table, 16 bytes per record. Read by FFX_Ch_LookupBonePoint and "
                      "FFX_Ch_UpdateBonePositions off CHRDATA.m_blobBase+32/+36.", "CONFIRMED"),
    3: ("unused",     "Never used. Offset is 0 in all 865 shipped files and no code reads "
                      "blobBase+40.", "CONFIRMED"),
    4: ("ptrtable",   "Array of pointers, aux = entry count. CHRDATA+0x114, aux -> CHRDATA+0x110 "
                      "(u16). Relocated by FFX_Mot_RelocateIdTable. Contents not decoded.", "CONFIRMED"),
    5: ("mot_field",  "Motion id table for mode 0 Field. dword[i] = motion id for logical "
                      "motion index i.", "CONFIRMED"),
    6: ("mot_fieldbtl", "Motion id table for mode 1 FieldBattle.", "CONFIRMED"),
    7: ("mot_swim",   "Motion id table for mode 2 Swim.", "CONFIRMED"),
    8: ("mot_swimbtl", "Motion id table for mode 3 SwimBattle.", "CONFIRMED"),
    9: ("params",     "Parameter block, aux = byte size. Read by FFX_Ch_InitChrDataFromBlob when "
                      "sectionCount > 9. Source of m_defaultScale and m_modelScale.", "CONFIRMED"),
    10: ("effect",    "Monster effect data. CHRDATA.m_effectDataSrc (+0x120), only read when "
                      "sectionCount > 10. Copied to a private heap block by "
                      "FFX_Ch_BlkAllocate.", "CONFIRMED"),
}

# Which section each motion mode resolves to. g_ffxMotModeToSetSlot at 0xC49784 is
# literally BYTE[4] {5,6,7,8}, and the mode names come from g_ffxDebugCharModeNames
# at 0xC34318. The four mot/resident<N>.mgrp banks next to the .chr are the same four
# modes, which is the independent cross-check.
MOTION_MODES = [(0, "Field", 5), (1, "FieldBattle", 6), (2, "Swim", 7), (3, "SwimBattle", 8)]

# Section 2 record: WORD[0] = kind<<14 | boneId, WORD[1] = joint node index, then 3 floats.
# The kind values are FFX_Ch_LookupBonePoint's return values.
BONEPOINT_KIND = {
    0x0000: "joint position, no offset",
    0x4000: "joint position + local offset",
    0x8000: "joint position, plain node",
    0xC000: "UNKNOWN (no shipped file uses it)",
}


class Section(object):
    """One top-level section of a .chr."""

    def __init__(self, index, offset, aux, data, end):
        self.index = index
        self.offset = offset      # file-relative, because header dword[0] is 0 on disk
        self.aux = aux            # a count for the table sections, a byte size for 9
        self.end = end            # start of the next populated section, or EOF
        self._data = data
        name, desc, conf = SECTIONS.get(index, ("sec%d" % index, "unknown section", "INFERRED"))
        self.name = name
        self.description = desc
        self.confidence = conf

    @property
    def present(self):
        return self.offset != 0

    @property
    def span(self):
        """Bytes from this section's start to the next one. Includes trailing 0x77 pad."""
        if not self.present:
            return 0
        return self.end - self.offset

    @property
    def payload(self):
        return self._data[self.offset:self.end] if self.present else b""

    @property
    def stride(self):
        """Bytes per record, where the section is a table.

        Sections 2 and 5-8 have a known stride read straight out of the exe. For the rest
        there is no record concept, so this returns None.
        """
        if self.index == 2:
            return 16
        if self.index in (5, 6, 7, 8):
            return 4
        return None

    @property
    def records(self):
        """Decoded records, for the sections whose layout is known."""
        if not self.present:
            return []
        if self.index == 2:
            out = []
            for i in range(self.aux):
                o = self.offset + 16 * i
                w0, joint = struct.unpack_from("<HH", self._data, o)
                x, y, z = struct.unpack_from("<3f", self._data, o + 4)
                out.append({
                    "index": i,
                    "bone_id": w0 & 0x3FFF,
                    "kind": w0 & 0xC000,
                    "kind_name": BONEPOINT_KIND.get(w0 & 0xC000, "?"),
                    "joint_node": joint,
                    "offset": (x, y, z),
                })
            return out
        if self.index in (5, 6, 7, 8):
            return list(struct.unpack_from("<%dI" % self.aux, self._data, self.offset))
        return []


class ModelHeader(object):
    """Section 0's own header.

    Every offset in here is relative to the START OF SECTION 0, not to the file.
    FFX_Ch_RelocateModelSection computes its delta as (sectionBase - storedBase) with
    storedBase 0, so it ends up adding the section base to each field.
    """

    def __init__(self, data, base):
        self.base = base
        f = lambda fmt, o: struct.unpack_from(fmt, data, base + o)[0]
        self.stored_base = f("<I", 0)        # CONFIRMED  0 on disk, overwritten at load
        self.revision = f("<H", 4)           # CONFIRMED  picks the mesh stride
        self.mesh_count = f("<H", 6)         # CONFIRMED  CHRPART array length
        self.count_08 = f("<H", 8)           # CONFIRMED  entries in the +20 table
        self.joint_count = f("<H", 10)       # CONFIRMED  352 and 396 bytes each are allocated
        self.count_12 = f("<H", 12)          # CONFIRMED  dwords in the +24 table
        self.u16_14 = f("<H", 14)            # INFERRED   unknown
        self.ptr_mesh_table = f("<I", 16)    # CONFIRMED  stride via mesh_stride
        self.ptr_20 = f("<I", 20)            # CONFIRMED  stride 12, count_08 entries
        self.ptr_24 = f("<I", 24)            # CONFIRMED  dword array, count_12 entries
        self.ptr_joints = f("<I", 28)        # CONFIRMED  stride 20 bind-pose table
        self.count_32 = f("<H", 32)          # CONFIRMED  entries in the +36 table
        self.ptr_36 = f("<I", 36)            # CONFIRMED  stride 12
        self.u32_40 = f("<I", 40)            # INFERRED   unknown, 0 in every pc file
        self.collision_count = f("<H", 44)   # CONFIRMED  96 bytes each are allocated
        self.ptr_48 = f("<I", 48)            # CONFIRMED  relocated, purpose unknown

    @property
    def mesh_stride(self):
        """FFX_Ch_MeshEntryAt 0x828BE0, verbatim."""
        return 40 if (self.revision >= 4884 or self.revision < 2097) else 24

    def joints(self, data):
        """The bind-pose table at +28: s16[10] per joint.

        Field meanings and the scale factors are taken straight out of
        FFX_Ch_BuildSkeletonInstance, which converts them as it fills the joint nodes.
        """
        out = []
        o = self.base + self.ptr_joints
        for i in range(self.joint_count):
            v = struct.unpack_from("<10h", data, o + 20 * i)
            out.append({
                "index": i,
                "parent": v[0],                                   # CONFIRMED
                "rot_deg": (v[1] / 100.0, v[2] / 100.0, v[3] / 100.0),   # CONFIRMED
                "translate": (v[4] / 1000.0, v[5] / 1000.0, v[6] / 1000.0),  # CONFIRMED
                "scale": (v[7] / 4096.0, v[8] / 4096.0, v[9] / 4096.0),      # CONFIRMED
            })
        return out


class ParamBlock(object):
    """Section 9. Read by FFX_Ch_InitChrDataFromBlob when sectionCount > 9.

    The CHRDATA destination offset is noted against each field. Names come from the
    recovered CHRDATA layout in reversing/ffx_types.h, so a field called f<hex> is a real
    field at a confirmed offset whose meaning is still open.
    """

    # (offset in block, CHRDATA offset, name, kind)
    LAYOUT = [
        (0,  0x54, "revision",       "u32"),
        (4,  0x1C, "m_f1C",          "f32"),
        (8,  0x2C, "m_f2C",          "f32"),
        (12, 0x30, "m_f30",          "f32"),
        (16, 0x20, "m_f20",          "f32"),
        (20, 0x28, "m_f28",          "f32"),
        (24, 0x24, "m_f24",          "f32"),
        (28, 0x34, "m_defaultScale", "f32"),
        (32, 0x38, "m_modelScale",   "f32"),
        (36, 0x40, "byte_40",        "u8"),
        (38, 0x42, "byte_42",        "u8"),
        (39, 0x43, "byte_43",        "u8"),
        # FFX_Ch_InitChrDataFromBlob copies these four as raw dwords and Hex-Rays happens
        # to type them int. The shipped bytes are plainly floats (0.0, -0.0, -0.242152,
        # 1.0 for c001), so read them as float.
        (40, 0x44, "f_44",           "f32"),
        (44, 0x48, "f_48",           "f32"),
        (48, 0x4C, "f_4C",           "f32"),
        (52, 0x50, "f_50",           "f32"),
    ]
    # Only read when the block's own revision dword is >= 5668.
    TAIL = [
        (56, 0x58, "f_58",    "f32"),
        (60, 0x5C, "f_5C",    "f32"),
        (64, 0x60, "f_60",    "f32"),
        (68, 0x64, "byte_64", "u8"),
        (69, 0x65, "byte_65", "u8"),
    ]

    def __init__(self, data, base, declared_size=None):
        self.base = base
        self.declared_size = declared_size
        self.revision = struct.unpack_from("<I", data, base)[0]   # CONFIRMED  gate value 5668
        self.has_tail = self.revision >= 5668
        self.truncated = []     # fields the buffer cannot supply

        avail = len(data) - base
        rows = list(self.LAYOUT) + (list(self.TAIL) if self.has_tail else [])
        self.fields = []
        for off, cd, nm, kind in rows:
            width = 1 if kind == "u8" else 4
            if off + width > avail:
                self.truncated.append(nm)
                continue
            fmt = {"u8": "<B", "u32": "<I", "f32": "<f"}[kind]
            self.fields.append((off, cd, nm, struct.unpack_from(fmt, data, base + off)[0]))

        # m_f30 also seeds CHRDATA+0x3C as 1000.0 / m_f30.
        self.m_f30 = struct.unpack_from("<f", data, base + 12)[0] if avail >= 16 else None
        self.derived_3C = (1000.0 / self.m_f30) if self.m_f30 else None

    @property
    def overruns_declared_size(self):
        """True when the game reads past the size the section table declares.

        Three shipped files do this: skl/k201, skl/k303 and skl/k403 declare a 32 or
        36 byte section 9 but FFX_Ch_InitChrDataFromBlob reads to +52 unconditionally
        once sectionCount > 9. It is harmless in the game only because FFX_Ch_RomRead
        over-allocates the blob buffer to ((size + 143) & ~0x7F) - 16.
        """
        if self.declared_size is None:
            return False
        need = 56 + (14 if self.has_tail else 0)
        return self.declared_size < need


class ChrFile(object):
    """A parsed .chr container."""

    def __init__(self, data, name="<mem>"):
        self.name = name
        self.data = data
        if len(data) < 16:
            raise ValueError("too short to be a .chr (%d bytes)" % len(data))

        # --- header, all CONFIRMED by FFX_Ch_RelocateChrBlob ---
        self.stored_base = struct.unpack_from("<I", data, 0)[0]
        self.section_count = struct.unpack_from("<I", data, 4)[0]
        self.version = struct.unpack_from("<I", data, 8)[0]
        self.unknown_0C = struct.unpack_from("<I", data, 12)[0]

        if self.section_count > 64:
            raise ValueError("implausible section count %d" % self.section_count)
        self.header_size = 16 + 8 * self.section_count

        raw = []
        for i in range(self.section_count):
            off, aux = struct.unpack_from("<2I", data, 16 + 8 * i)
            raw.append((off, aux))

        # A section's span runs to the next populated section, so sort the offsets once.
        starts = sorted(o for o, _ in raw if o)
        self.sections = []
        for i, (off, aux) in enumerate(raw):
            end = len(data)
            if off:
                later = [s for s in starts if s > off]
                if later:
                    end = later[0]
            self.sections.append(Section(i, off, aux, data, end))

        self.model = None
        if self.sections[0].present:
            self.model = ModelHeader(data, self.sections[0].offset)
        self.params = None
        if self.section_count > 9 and self.sections[9].present:
            self.params = ParamBlock(data, self.sections[9].offset, self.sections[9].aux)

    # ---- construction helpers ----

    @staticmethod
    def _resolve(path):
        """Accept 'pc/c001', 'pc/c001/mdl/c001.chr' or a full archive path."""
        p = path.replace("\\", "/").lstrip("/")
        if p.startswith("ffx_ps2/"):
            return p
        if p.endswith(".chr"):
            return CHR_ROOT + p
        # 'pc/c001' -> 'pc/c001/mdl/c001.chr'
        bits = p.rstrip("/").split("/")
        return CHR_ROOT + "%s/%s/mdl/%s.chr" % (bits[0], bits[1], bits[1])

    @classmethod
    def from_archive(cls, path, archive=None):
        ar = archive or vbf.VbfArchive(vbf.DEFAULT_VBF)
        full = cls._resolve(path)
        return cls(ar.read(full), full)

    @classmethod
    def from_file(cls, path):
        with open(path, "rb") as fh:
            return cls(fh.read(), path)

    # ---- reporting ----

    def pad_after_header(self):
        """Bytes between the header and the first section, which should all be 0x77."""
        starts = sorted(s.offset for s in self.sections if s.present)
        if not starts:
            return b""
        return self.data[self.header_size:starts[0]]

    def report(self, limit=12):
        L = []
        a = L.append
        a("=" * 78)
        a("%s  (%d bytes)" % (self.name, len(self.data)))
        a("=" * 78)
        a("")
        a("HEADER                                                    status")
        a("  +0x00  storedBase       0x%08X   %-24s %s"
          % (self.stored_base,
             "(0 on disk; the loader overwrites it with the load address)",
             "CONFIRMED"))
        a("  +0x04  sectionCount     %-12d %-24s %s"
          % (self.section_count, "loop bound in FFX_Ch_RelocateChrBlob", "CONFIRMED"))
        a("  +0x08  version          %-12d %-24s %s"
          % (self.version, "2 -> 11 sections, 1 -> 10", "INFERRED"))
        a("  +0x0C  unknown          0x%08X   %-24s %s"
          % (self.unknown_0C, "0 in all 865 files, no reader found", "INFERRED"))
        a("  header ends at 0x%X, then 0x%X pad bytes (%s)"
          % (self.header_size, len(self.pad_after_header()),
             "all 0x77" if self.pad_after_header() and
             all(b == PAD_BYTE for b in self.pad_after_header())
             else "empty" if not self.pad_after_header() else "NOT all 0x77"))
        a("")
        a("SECTION TABLE  (descriptor i at +0x%X + 8*i: u32 offset, u32 aux)" % 16)
        a("  aux means different things per section, each checked across all 865 files:")
        a("    sec0  always 0, unused         sec1  small count, 1..6, meaning unknown")
        a("    sec2  record count, stride 16  sec4  pointer count, stride 4")
        a("    sec5-8 motion id count, stride 4")
        a("    sec9  byte size (72 current, 16/28/32/36/56 on older revisions)")
        a("    sec10 byte size, exact")
        a("  idx name          offset      aux     span      status")
        for s in self.sections:
            if s.present:
                a("  %-3d %-13s 0x%08X  %-7d 0x%-7X %s"
                  % (s.index, s.name, s.offset, s.aux, s.span, s.confidence))
            else:
                a("  %-3d %-13s %-11s %-7s %-9s %s"
                  % (s.index, s.name, "-", "-", "-", s.confidence))
        a("")
        for s in self.sections:
            a("  [%d] %s: %s" % (s.index, s.name, s.description))
        a("")

        if self.model:
            m = self.model
            a("SECTION 0 - MODEL / SKELETON HEADER  (offsets relative to 0x%X, the section base)"
              % m.base)
            a("  +0x00  storedBase       0x%08X                      CONFIRMED" % m.stored_base)
            a("  +0x04  revision         %-6d -> mesh stride %-2d        CONFIRMED"
              % (m.revision, m.mesh_stride))
            a("  +0x06  meshCount        %-6d (56-byte CHRPART each)   CONFIRMED" % m.mesh_count)
            a("  +0x08  count_08         %-6d (entries in the +20 tbl) CONFIRMED" % m.count_08)
            a("  +0x0A  jointCount       %-6d (352 + 396 bytes each)   CONFIRMED" % m.joint_count)
            a("  +0x0C  count_12         %-6d (dwords in the +24 tbl)  CONFIRMED" % m.count_12)
            a("  +0x0E  unknown          %-6d                          INFERRED" % m.u16_14)
            a("  +0x10  meshTable      ->0x%08X                      CONFIRMED" % m.ptr_mesh_table)
            a("  +0x14  table_20       ->0x%08X  stride 12            CONFIRMED" % m.ptr_20)
            a("  +0x18  table_24       ->0x%08X  dwords               CONFIRMED" % m.ptr_24)
            a("  +0x1C  jointTable     ->0x%08X  stride 20            CONFIRMED" % m.ptr_joints)
            a("  +0x20  count_32         %-6d (entries in the +36 tbl) CONFIRMED" % m.count_32)
            a("  +0x24  table_36       ->0x%08X  stride 12            CONFIRMED" % m.ptr_36)
            a("  +0x28  unknown          0x%08X                      INFERRED" % m.u32_40)
            a("  +0x2C  collisionCount   %-6d (96 bytes each)         CONFIRMED" % m.collision_count)
            a("  +0x30  table_48       ->0x%08X                      CONFIRMED" % m.ptr_48)
            a("")
            js = m.joints(self.data)
            a("  joint bind-pose table, first %d of %d  (s16[10]: parent, rot*100 deg, "
              "translate*1000, scale*4096)" % (min(limit, len(js)), len(js)))
            for j in js[:limit]:
                a("    %3d parent %4d  rot (%8.2f %8.2f %8.2f)  t (%8.3f %8.3f %8.3f)  "
                  "s (%.3f %.3f %.3f)"
                  % (j["index"], j["parent"], j["rot_deg"][0], j["rot_deg"][1], j["rot_deg"][2],
                     j["translate"][0], j["translate"][1], j["translate"][2],
                     j["scale"][0], j["scale"][1], j["scale"][2]))
            if len(js) > limit:
                a("    ... %d more" % (len(js) - limit))
            a("")

        bp = self.sections[2]
        if bp.present:
            recs = bp.records
            a("SECTION 2 - BONE POINT TABLE  (%d records of 16 bytes)" % len(recs))
            a("  idx boneId kind    joint  local offset                    meaning")
            for r in recs[:limit]:
                a("  %3d %6d 0x%04X  %5d  (%9.4f %9.4f %9.4f)  %s"
                  % (r["index"], r["bone_id"], r["kind"], r["joint_node"],
                     r["offset"][0], r["offset"][1], r["offset"][2], r["kind_name"]))
            if len(recs) > limit:
                a("  ... %d more" % (len(recs) - limit))
            a("  Bone ids are the logical slots indexed into CHR.m_boneWorldPos (22 float4s at")
            a("  CHR+0x524), so ids above 21 are addressable only through "
              "FFX_Ch_LookupBonePoint.")
            a("")

        a("MOTION ID TABLES  (g_ffxMotModeToSetSlot 0xC49784 = {5,6,7,8})")
        for mode, mname, sidx in MOTION_MODES:
            s = self.sections[sidx] if sidx < len(self.sections) else None
            if s is None or not s.present:
                a("  mode %d %-12s section %d  absent  (FFX_Mot_SetByModeIndex falls back to "
                  "mode 0)" % (mode, mname, sidx))
                continue
            ids = s.records
            a("  mode %d %-12s section %d  %d entries" % (mode, mname, sidx, len(ids)))
            shown = ids[:limit]
            a("      " + "  ".join("[%d]=0x%08X" % (i, v) for i, v in enumerate(shown)))
            if len(ids) > limit:
                a("      ... %d more" % (len(ids) - limit))
        a("  A motion id is (modelId << 16) | motionIndex. FFX_Ch_Allocate seeds "
          "CHR.m_slots[0..2]")
        a("  with logical indices 0/1/2, which resolve through these tables to idle / walk / run.")
        a("")

        if self.params:
            p = self.params
            a("SECTION 9 - PARAMETER BLOCK  (at 0x%X, %d bytes declared)"
              % (p.base, self.sections[9].aux))
            a("  revision %d, tail fields %s"
              % (p.revision, "present (>= 5668)" if p.has_tail else "absent (< 5668)"))
            if p.overruns_declared_size:
                a("  WARNING: the game reads to +%d but the section table declares only %d bytes."
                  % (56 + (14 if p.has_tail else 0), p.declared_size))
                a("  It survives only because FFX_Ch_RomRead over-allocates the blob buffer.")
            if p.truncated:
                a("  WARNING: past end of file, not shown: %s" % ", ".join(p.truncated))
            a("  off  ->CHRDATA  name              value")
            for off, cd, nm, val in p.fields:
                if isinstance(val, float):
                    if val != val or val in (float("inf"), float("-inf")):
                        # Not a real float. FFX_Ch_InitChrDataFromBlob seeds CHRDATA+0x60
                        # with the literal 0x7F808080, so that value is a sentinel meaning
                        # "unset" rather than a number.
                        raw = struct.unpack("<I", struct.pack("<f", val))[0]
                        note = " (sentinel, matches the 0x7F808080 default)" \
                            if raw == 0x7F808080 else " (not a finite float)"
                        a("  +%-3d  +0x%02X      %-17s 0x%08X%s" % (off, cd, nm, raw, note))
                    else:
                        a("  +%-3d  +0x%02X      %-17s %.6g" % (off, cd, nm, val))
                else:
                    a("  +%-3d  +0x%02X      %-17s %d" % (off, cd, nm, val))
            if p.derived_3C is not None:
                a("       +0x3C      (derived)         %.6g   = 1000.0 / m_f30"
                  % p.derived_3C)
            a("  Every row above is CONFIRMED - FFX_Ch_InitChrDataFromBlob 0x825EE0 copies each")
            a("  one to the CHRDATA offset shown. m_defaultScale is what FFX_Ch_Allocate feeds to")
            a("  FFX_Ch_SetScaleUniform, so it is the spawn scale of a cloned CHR.")
            a("")

        eff = self.sections[10] if self.section_count > 10 else None
        if eff is not None and eff.present:
            a("SECTION 10 - EFFECT DATA  at 0x%X, %d bytes to EOF" % (eff.offset, eff.span))
            a("  CHRDATA.m_effectDataSrc. FFX_Ch_BlkAllocate memcpys it into a private heap")
            a("  block, so this one section is NOT shared between two CHRs of the same id.")
            a("  Internal layout: UNKNOWN.")
            a("")

        a("KNOWN UNKNOWNS for this file")
        a("  - header +0x0C: always 0, no reader found.")
        a("  - section 3: always absent, no reader found.")
        a("  - section 4 contents: the pointers are relocated but nothing was traced reading them.")
        a("  - section 0 fields +0x0E and +0x28, and the purpose of tables +0x14 / +0x18 / "
          "+0x24 / +0x30.")
        a("  - section 1 and section 10 internal layouts.")
        return "\n".join(L)


# ------------------------------------------------------------------ commands

def _all_chr(ar):
    return sorted((e for e in ar.entries if e.name.endswith(".chr")), key=lambda e: e.name)


def cmd_list(args, ar):
    for e in _all_chr(ar):
        rel = e.name[len(CHR_ROOT):] if e.name.startswith(CHR_ROOT) else e.name
        if args.filter:
            # A filter ending in '/' anchors on the category, so 'pc/' does not also
            # match 'npc/'. Anything else is a plain substring.
            f = args.filter
            if f.endswith("/"):
                if not rel.startswith(f):
                    continue
            elif f not in rel:
                continue
        print("%10d  %s" % (e.size, rel))


def cmd_dump(args, ar):
    c = ChrFile.from_file(args.path) if args.file else ChrFile.from_archive(args.path, ar)
    print(c.report(limit=args.limit))


def cmd_verify(args, ar):
    files = _all_chr(ar)
    problems = 0
    stats = {}
    for e in files:
        try:
            c = ChrFile(ar.read(e), e.name)
        except Exception as ex:
            print("PARSE FAIL  %s: %s" % (e.name, ex))
            problems += 1
            continue
        if c.stored_base != 0:
            print("storedBase != 0  %s: 0x%X" % (e.name, c.stored_base))
            problems += 1
        pad = c.pad_after_header()
        if pad and not all(b == PAD_BYTE for b in pad):
            print("header pad not 0x77  %s" % e.name)
            problems += 1
        for s in c.sections:
            if s.present and not (c.header_size <= s.offset <= len(c.data)):
                print("section %d out of range  %s" % (s.index, e.name))
                problems += 1
            # Where the stride is known, the span must be a whole number of records.
            if s.present and s.stride and s.span < s.aux * s.stride:
                print("section %d span too small for %d x %d  %s"
                      % (s.index, s.aux, s.stride, e.name))
                problems += 1
        stats.setdefault(c.section_count, 0)
        stats[c.section_count] += 1
    print()
    print("%d files parsed, %d problems" % (len(files), problems))
    print("section count distribution: %s" % stats)


def main(argv=None):
    ap = argparse.ArgumentParser(description="dump the PS2-era .chr containers of FFX HD")
    ap.add_argument("--vbf", default=vbf.DEFAULT_VBF)
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("list", help="list the .chr files in the archive")
    p.add_argument("filter", nargs="?", default=None)
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser("dump", help="dump one .chr")
    p.add_argument("path", help="'pc/c001', an archive path, or a local file with --file")
    p.add_argument("--file", action="store_true", help="read from disk, not the archive")
    p.add_argument("--limit", type=int, default=12, help="rows per table (default 12)")
    p.set_defaults(fn=cmd_dump)

    p = sub.add_parser("verify", help="parse every .chr and report anomalies")
    p.set_defaults(fn=cmd_verify)

    args = ap.parse_args(argv)
    if not getattr(args, "fn", None):
        ap.print_help()
        return 1
    needs_archive = not (args.cmd == "dump" and args.file)
    ar = vbf.VbfArchive(args.vbf) if needs_archive else None
    args.fn(args, ar)
    return 0


if __name__ == "__main__":
    sys.exit(main())
