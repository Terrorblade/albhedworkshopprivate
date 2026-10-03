#!/usr/bin/env python3
"""
.mgrp reader for FINAL FANTASY X HD Remaster.

An .mgrp ("motion group") is a relocatable bundle of motion banks. The game loads one
whole and fixes its internal offsets up into pointers in place, so every "offset" below
is a file offset exactly as stored - the on-disk relocation anchor is 0, which makes the
baked pointers file-relative.

Who loads them, for reference (VAs at the preferred base 0x400000):

    FFX_Ch_RomReadMotionSet       0x82A4C0   async read of <chr>/mot/resident<mode>.mgrp
    FFX_Ch_LoadMotionSetSync      0x836870   -> FFX_Mot_RegisterBundle
    FFX_Mot_RegisterBundle        0x836C90   registry slot + FFX_Mot_RelocateBundle + per-bank register
    FFX_Mot_RelocateBundle        0x836F70   offsets -> pointers (header level)
    FFX_Mot_RelocateGroupBank     0x836FC0   offsets -> pointers (bank level)
    FFX_Mot_RegisterGroup         0x836D30   one bank -> g_ffxMotGroupTable 0x1300A08
    FFX_Ch_SetMotionKey           0x837AC0   motion id -> bank + motion entry + sequence PC
    FFX_Mot_SeqExec               0x837900   the sequence bytecode interpreter
    FFX_Mot_StartClip             0x839B80   PLAY -> clip
    FFX_Mot_BindChannels          0x839980   clip -> per-CHR animated channels

Everything marked UNKNOWN below is a field no code path has been observed to read.

Usage:
    python mgrp.py list [substring]
    python mgrp.py dump <archive-path-or-local-file> [--seq] [--clips] [--bank N]
    python mgrp.py verify [substring]
"""

import argparse
import os
import struct
import sys

try:
    import vbf
except ImportError:  # running from another directory
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import vbf


DEFAULT_ARCHIVE = (
    r"G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\data\FFX_Data.vbf"
)

HEADER_SIZE = 16
BANK_SIZE = 20
ENTRY_SIZE = 16


# ---------------------------------------------------------------------------
# sequence bytecode
# ---------------------------------------------------------------------------

# opcode -> (name, total byte length including the opcode byte)
# From FFX_Mot_SeqExec 0x837900. Lengths are how far the interpreter moves the PC.
SEQ_OPS = {
    0: ("END", 1),              # stops, PC left pointing at the END byte
    1: ("PLAY", 9),             # 4 x u16: clipIdx, loopCount, startMarker, endMarker
    2: ("WAIT_CLIP_END", 1),    # blocks while CHR+0x728 (m_clipPlaying) != 0
    3: ("WAIT_FRAMES", 3),      # u16 n, counted down in CHR+0x72A
    4: ("JUMP", 3),             # u16 labelIdx -> bytecodeBase + labelTable[labelIdx]
    5: ("STOP", 1),             # sets CHR+0x734, PC left in place
    6: ("WAIT_KEY", 3),         # blocks while (int8)CHR+0x737 < n
}


class SeqOp(object):
    __slots__ = ("offset", "opcode", "name", "size", "args", "raw")

    def __init__(self, offset, opcode, name, size, args, raw):
        self.offset = offset
        self.opcode = opcode
        self.name = name
        self.size = size
        self.args = args
        self.raw = raw

    def text(self):
        if self.name == "PLAY":
            clip, loops, start, end = self.args
            loop_note = "forever" if loops == 0 else "x%d" % loops
            return "PLAY clip=%d loop=%s startMarker=%d endMarker=%d" % (
                clip, loop_note, start, end)
        if self.name == "WAIT_FRAMES":
            return "WAIT_FRAMES %d" % self.args[0]
        if self.name == "JUMP":
            return "JUMP label=%d" % self.args[0]
        if self.name == "WAIT_KEY":
            return "WAIT_KEY >= %d" % self.args[0]
        if self.name == "???":
            return "??? opcode 0x%02x" % self.opcode
        return self.name

    def __repr__(self):
        return "<%04x %s>" % (self.offset, self.text())


def decode_sequence(data, offset, max_len=None, max_ops=512):
    """Linear-decode the sequence bytecode at `offset`.

    Stops after END or STOP, after max_len bytes, or when an unknown opcode turns up.
    JUMP is not followed - this is a listing, not a trace.
    """
    ops = []
    o = offset
    limit = offset + max_len if max_len is not None else len(data)
    while o < len(data) and o < limit and len(ops) < max_ops:
        opcode = data[o]
        name, size = SEQ_OPS.get(opcode, ("???", 1))
        raw = data[o:o + size]
        args = tuple(struct.unpack_from("<%dH" % ((size - 1) // 2), data, o + 1)) \
            if size > 1 else ()
        ops.append(SeqOp(o, opcode, name, size, args, raw))
        o += size
        if name in ("END", "STOP", "???"):
            break
    return ops


# ---------------------------------------------------------------------------
# animation clip header (the thing a PLAY opcode ends up handing to the engine)
# ---------------------------------------------------------------------------

class AnimClip(object):
    """Header of one compressed animation clip.

    Layout from FFX_Mot_BindChannels 0x839980 and FFX_Mot_StartClip 0x839B80.
    CONFIRMED fields are the ones those two functions read.
    """

    __slots__ = ("offset", "frames", "joints", "unk4", "fps", "event_count",
                 "bitstream_off", "valuestream_off", "event_off", "raw")

    def __init__(self, data, offset):
        self.offset = offset
        (self.frames, self.joints) = struct.unpack_from("<2H", data, offset)
        self.unk4 = data[offset + 4]          # UNKNOWN - 0 in every clip sampled
        self.fps = data[offset + 5]           # frame rate; CHR+0x7E0 = the u16 at +4
        self.event_count = struct.unpack_from("<H", data, offset + 6)[0]
        (rel_bits, rel_vals, rel_events) = struct.unpack_from("<3I", data, offset + 8)
        self.bitstream_off = offset + rel_bits
        self.valuestream_off = offset + rel_vals
        self.event_off = (offset + rel_events) if self.event_count else None
        self.raw = data[offset:offset + 20]

    def describe(self):
        return ("frames=%d joints=%d fps=%d events=%d bits@%06x vals@%06x "
                "evt@%s unk4=%d" % (
                    self.frames, self.joints, self.fps, self.event_count,
                    self.bitstream_off, self.valuestream_off,
                    ("%06x" % self.event_off) if self.event_off else "-",
                    self.unk4))


# ---------------------------------------------------------------------------
# containers
# ---------------------------------------------------------------------------

class MotionEntry(object):
    """One 16-byte record of a bank's MOTION table (bank+0x0C).

    +0x00 u16 motion index      CONFIRMED - FFX_Ch_SetMotionKey matches LOWORD(motionId) on this
    +0x02 u16 model id          CONFIRMED by data (always equals the bank's model id)
    +0x04 u16 label count       INFERRED - p12 - p8 == 2 * this for all 22125 shipped entries
    +0x06 u16 bytecode length   INFERRED - exact for 22113 of 22125, over by 7 on 12 entries
    +0x08 u32 label table       CONFIRMED - FFX_Mot_SeqExec JUMP reads (u16*)this [labelIdx]
    +0x0C u32 sequence bytecode CONFIRMED - becomes CHR+0x714, the sequence PC
    """

    __slots__ = ("offset", "index", "model_id", "label_count", "seq_len",
                 "label_off", "seq_off", "labels", "ops")

    def __init__(self, data, offset):
        self.offset = offset
        (self.index, self.model_id, self.label_count, self.seq_len,
         self.label_off, self.seq_off) = struct.unpack_from("<4H2I", data, offset)
        self.labels = list(struct.unpack_from(
            "<%dH" % self.label_count, data, self.label_off)) if self.label_count else []
        self.ops = decode_sequence(data, self.seq_off, self.seq_len)

    def motion_id(self):
        return (self.model_id << 16) | self.index

    def describe(self):
        return ("motion %04x (id %08x) labels=%d seqlen=%d seq@%06x" % (
            self.index, self.motion_id(), self.label_count, self.seq_len, self.seq_off))


class ClipEntry(object):
    """One 16-byte record of a bank's CLIP table (bank+0x10), the PLAY target.

    +0x00 u16 UNKNOWN - 0 in all 21165 shipped entries
    +0x02 u16 UNKNOWN - 0 in all 21165 shipped entries
    +0x04 u16 frame marker count  INFERRED - (p12 - p8) >= 2 * this always holds
    +0x06 u16 UNKNOWN - 0 in all 21165 shipped entries
    +0x08 u32 frame marker array  CONFIRMED - PLAY's startMarker/endMarker index this (u16, signed)
    +0x0C u32 animation clip      CONFIRMED - handed to FFX_Mot_StartClip as animData
    """

    __slots__ = ("offset", "unk0", "unk2", "marker_count", "unk6",
                 "marker_off", "clip_off", "markers", "clip")

    def __init__(self, data, offset):
        self.offset = offset
        (self.unk0, self.unk2, self.marker_count, self.unk6,
         self.marker_off, self.clip_off) = struct.unpack_from("<4H2I", data, offset)
        self.markers = list(struct.unpack_from(
            "<%dh" % self.marker_count, data, self.marker_off)) if self.marker_count else []
        self.clip = AnimClip(data, self.clip_off) if self.clip_off else None

    def describe(self):
        return "markers=%s clip@%06x %s" % (
            self.markers, self.clip_off, self.clip.describe() if self.clip else "")


class Bank(object):
    """One 20-byte group-bank descriptor. The array lives at header+0x0C.

    +0x00 u32 UNKNOWN - 0 in 4142 of 4406 shipped banks, otherwise a 0x40xxxxxx value that
                        looks like a leftover PS2 EE build address. Never read by the exe.
    +0x04 u16 model id          CONFIRMED - FFX_Mot_RegisterGroup copies it to the group table
    +0x06 u16 UNKNOWN - 0 in all 4406 shipped banks
    +0x08 u16 motion count      CONFIRMED - FFX_Mot_RelocateGroupBank / FFX_Ch_SetMotionKey
    +0x0A u16 clip count        CONFIRMED - FFX_Mot_RelocateGroupBank
    +0x0C u32 motion table      CONFIRMED - 16-byte MotionEntry records
    +0x10 u32 clip table        CONFIRMED - 16-byte ClipEntry records, the PLAY base
    """

    __slots__ = ("index", "offset", "unk0", "model_id", "unk6",
                 "motion_count", "clip_count", "motion_off", "clip_off",
                 "motions", "clips")

    def __init__(self, data, offset, index):
        self.index = index
        self.offset = offset
        (self.unk0, self.model_id, self.unk6, self.motion_count, self.clip_count,
         self.motion_off, self.clip_off) = struct.unpack_from("<I4H2I", data, offset)
        self.motions = [MotionEntry(data, self.motion_off + i * ENTRY_SIZE)
                        for i in range(self.motion_count)]
        self.clips = [ClipEntry(data, self.clip_off + i * ENTRY_SIZE)
                      for i in range(self.clip_count)]

    def model_name(self):
        return model_id_to_name(self.model_id)

    def describe(self):
        return ("bank %d @%06x model=%04x (%s) motions=%d clips=%d "
                "motionTbl@%06x clipTbl@%06x unk0=%08x unk6=%d" % (
                    self.index, self.offset, self.model_id, self.model_name(),
                    self.motion_count, self.clip_count,
                    self.motion_off, self.clip_off, self.unk0, self.unk6))


class Mgrp(object):
    """The whole container.

    +0x00 u32 relocation anchor CONFIRMED - 0 on disk in all 3156 shipped files.
                                FFX_Mot_RelocateBundle computes delta = &hdr - hdr[0] and
                                stamps hdr[0] = &hdr, which makes the pass idempotent.
    +0x04 u32 bank count        CONFIRMED
    +0x08 u32 UNKNOWN - 0 in all 3156 shipped files, never read by the exe
    +0x0C u32 bank table offset CONFIRMED - bank_count * 20 bytes, always at the tail of the file
    """

    def __init__(self, data, name="<mem>"):
        if len(data) < HEADER_SIZE:
            raise ValueError("%s: too small to be an mgrp (%d bytes)" % (name, len(data)))
        self.name = name
        self.data = data
        (self.anchor, self.bank_count, self.unk8,
         self.bank_table_off) = struct.unpack_from("<4I", data, 0)
        if self.anchor != 0:
            raise ValueError("%s: relocation anchor is %#x, expected 0 on disk"
                             % (name, self.anchor))
        end = self.bank_table_off + self.bank_count * BANK_SIZE
        if self.bank_count and end > len(data):
            raise ValueError("%s: bank table runs past EOF (%#x > %#x)"
                             % (name, end, len(data)))
        self.banks = [Bank(data, self.bank_table_off + i * BANK_SIZE, i)
                      for i in range(self.bank_count)]

    def is_empty(self):
        return self.bank_count == 0

    def find_motion(self, motion_id):
        """Mirror FFX_Ch_SetMotionKey: first bank with the model id, first matching index."""
        model = (motion_id >> 16) & 0xFFFF
        index = motion_id & 0xFFFF
        for bank in self.banks:
            if bank.model_id != model:
                continue
            for entry in bank.motions:
                if entry.index == index:
                    return bank, entry
        return None, None


# ---------------------------------------------------------------------------
# small helpers
# ---------------------------------------------------------------------------

# FFX_Ch_IdToModelName 0x838100 / FFX_Ch_CategoryToLetter: id >> 12 is the category.
CATEGORY_LETTER = {0: "c", 1: "m", 2: "n", 3: "s", 4: "w", 5: "o", 6: "k", 0xF: "v"}


def model_id_to_name(model_id):
    cat = (model_id >> 12) & 0xF
    return "%s%03d" % (CATEGORY_LETTER.get(cat, "?"), model_id & 0xFFF)


def open_archive(path=None):
    return vbf.VbfArchive(path or DEFAULT_ARCHIVE)


def load(path, archive=None):
    """Read an .mgrp from the VBF archive, or from the local filesystem if it exists."""
    if os.path.isfile(path):
        with open(path, "rb") as fh:
            return Mgrp(fh.read(), path)
    arc = archive if archive is not None else open_archive()
    return Mgrp(arc.read(path), path)


def iter_archive(archive, substring=None):
    for entry in archive.entries:
        if not entry.name.endswith(".mgrp"):
            continue
        if substring and substring not in entry.name:
            continue
        yield entry


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------

def cmd_list(args):
    arc = open_archive(args.archive)
    rows = list(iter_archive(arc, args.substring))
    rows.sort(key=lambda e: e.name)
    for entry in rows:
        tag = " (empty)" if entry.size == HEADER_SIZE else ""
        print("%10d  %s%s" % (entry.size, entry.name, tag))
    print("%d file(s)" % len(rows))


def cmd_dump(args):
    arc = None if os.path.isfile(args.path) else open_archive(args.archive)
    m = load(args.path, arc)
    print("%s  %d bytes" % (m.name, len(m.data)))
    print("header: anchor=%#x bankCount=%d unk8=%#x bankTable=%#06x" % (
        m.anchor, m.bank_count, m.unk8, m.bank_table_off))
    if m.is_empty():
        print("  (no banks - an empty placeholder set)")
        return
    for bank in m.banks:
        if args.bank is not None and bank.index != args.bank:
            continue
        print("  " + bank.describe())
        for i, entry in enumerate(bank.motions):
            print("    M%03d @%06x %s" % (i, entry.offset, entry.describe()))
            if entry.labels:
                print("          labels: %s" % entry.labels)
            if args.seq:
                for op in entry.ops:
                    print("          %06x  %-24s %s" % (
                        op.offset, op.raw.hex(" "), op.text()))
        if args.clips:
            for i, clip in enumerate(bank.clips):
                print("    C%03d @%06x %s" % (i, clip.offset, clip.describe()))
                if clip.unk0 or clip.unk2 or clip.unk6:
                    print("          UNKNOWN non-zero: +0=%d +2=%d +6=%d" % (
                        clip.unk0, clip.unk2, clip.unk6))


def cmd_verify(args):
    """Re-check every structural assumption against the archive."""
    arc = open_archive(args.archive)
    rows = list(iter_archive(arc, args.substring))
    stats = {
        "files": 0, "empty": 0, "banks": 0, "motions": 0, "clips": 0,
        "label_count_ok": 0, "label_count_bad": 0,
        "seq_len_ok": 0, "seq_len_bad": 0,
        "marker_room_ok": 0, "marker_room_bad": 0,
        "a_model_matches_bank": 0, "a_model_mismatch": 0,
        "clip_unk_nonzero": 0, "bank_unk6_nonzero": 0, "hdr_unk8_nonzero": 0,
    }
    opcodes = {}
    failures = []
    for entry in rows:
        stats["files"] += 1
        try:
            m = Mgrp(arc.read(entry.name), entry.name)
        except Exception as exc:  # structural rejection
            failures.append("%s: %s" % (entry.name, exc))
            continue
        if m.unk8:
            stats["hdr_unk8_nonzero"] += 1
        if m.is_empty():
            stats["empty"] += 1
            if len(m.data) != HEADER_SIZE:
                failures.append("%s: 0 banks but %d bytes" % (entry.name, len(m.data)))
            continue
        for bank in m.banks:
            stats["banks"] += 1
            if bank.unk6:
                stats["bank_unk6_nonzero"] += 1
            for em in bank.motions:
                stats["motions"] += 1
                key = "a_model_matches_bank" if em.model_id == bank.model_id \
                    else "a_model_mismatch"
                stats[key] += 1
                room = em.seq_off - em.label_off
                stats["label_count_ok" if room == 2 * em.label_count
                      else "label_count_bad"] += 1
                walked = sum(op.size for op in em.ops)
                term = em.ops[-1].name if em.ops else None
                exact = walked == em.seq_len and term in ("END", "STOP")
                stats["seq_len_ok" if exact else "seq_len_bad"] += 1
                if not exact:
                    failures.append(
                        "%s bank%d motion %04x: seq_len=%d walked=%d term=%s"
                        % (entry.name, bank.index, em.index, em.seq_len, walked, term))
                for op in em.ops:
                    opcodes[op.name] = opcodes.get(op.name, 0) + 1
            for ec in bank.clips:
                stats["clips"] += 1
                if ec.unk0 or ec.unk2 or ec.unk6:
                    stats["clip_unk_nonzero"] += 1
                room = ec.clip_off - ec.marker_off
                stats["marker_room_ok" if room >= 2 * ec.marker_count
                      else "marker_room_bad"] += 1
    for key in sorted(stats):
        print("%-24s %d" % (key, stats[key]))
    print("opcodes: %s" % sorted(opcodes.items(), key=lambda kv: -kv[1]))
    print("%d structural failure(s)" % len(failures))
    for line in failures[:args.max_failures]:
        print("  " + line)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--archive", default=None, help="VBF path (default: FFX_Data.vbf)")
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("list", help="list .mgrp files in the archive")
    p.add_argument("substring", nargs="?")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("dump", help="dump one .mgrp")
    p.add_argument("path")
    p.add_argument("--seq", action="store_true", help="disassemble the sequence bytecode")
    p.add_argument("--clips", action="store_true", help="list the clip table")
    p.add_argument("--bank", type=int, default=None)
    p.set_defaults(func=cmd_dump)

    p = sub.add_parser("verify", help="re-check the format over the whole archive")
    p.add_argument("substring", nargs="?")
    p.add_argument("--max-failures", type=int, default=20)
    p.set_defaults(func=cmd_verify)

    args = ap.parse_args(argv)
    if not getattr(args, "func", None):
        ap.print_help()
        return 2
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
