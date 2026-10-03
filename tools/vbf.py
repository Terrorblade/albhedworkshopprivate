"""
Reader for the VBF archives that ship with FINAL FANTASY X/X-2 HD Remaster.

The format was recovered from FFX_VbfArchive__open at VA 0x61DAC0 in FFX.exe and then
checked byte for byte against the shipped data/FFX_Data.vbf. See reversing/VBF_FORMAT.md
for the layout and for how each field was pinned down.

Usage:
    python vbf.py info
    python vbf.py list [--out names.txt]
    python vbf.py find <substring>
    python vbf.py cat <path>                       write one file to stdout
    python vbf.py extract <path> [-o OUT]
    python vbf.py extract-all <substring> -o DIR   every path containing the substring
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

MAGIC = 0x4B595253          # "SRYK" in file byte order
CHUNK = 0x10000             # every file is split into 64 KiB logical chunks

DEFAULT_VBF = r"G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\data\FFX_Data.vbf"


class VbfEntry(object):
    __slots__ = ("index", "name", "key", "first_chunk", "size", "offset", "flag4")

    def __init__(self, index, name, key, first_chunk, size, offset, flag4):
        self.index = index
        self.name = name
        self.key = key
        self.first_chunk = first_chunk
        self.size = size
        self.offset = offset
        self.flag4 = flag4

    @property
    def chunk_count(self):
        return (self.size + CHUNK - 1) // CHUNK

    def __repr__(self):
        return "<VbfEntry %s size=%d off=0x%X>" % (self.name, self.size, self.offset)


class VbfArchive(object):
    def __init__(self, path):
        self.path = path
        self._f = open(path, "rb")
        magic, header_size = struct.unpack("<II", self._f.read(8))
        if magic != MAGIC:
            raise ValueError("not a VBF: magic is 0x%08X, expected 0x%08X" % (magic, MAGIC))
        self.header_size = header_size
        self._f.seek(0)
        hdr = self._f.read(header_size)
        if len(hdr) != header_size:
            raise ValueError("truncated header")

        self._f.seek(0, os.SEEK_END)
        self.file_size = self._f.tell()
        self._f.seek(-16, os.SEEK_END)
        self.stored_md5 = self._f.read(16)

        count = struct.unpack_from("<I", hdr, 8)[0]
        key_off = 16
        ent_off = key_off + 16 * count
        blob_len_off = ent_off + 32 * count
        blob_len = struct.unpack_from("<I", hdr, blob_len_off)[0]
        blob_off = blob_len_off + 4
        chunk_off = blob_len_off + blob_len

        self.chunk_table_offset = chunk_off
        self.chunk_count = (header_size - chunk_off) // 2
        self._chunk_sizes = struct.unpack_from("<%dH" % self.chunk_count, hdr, chunk_off)

        self.entries = []
        self.by_name = {}
        self.by_key = {}
        for i in range(count):
            o = ent_off + 32 * i
            first_chunk, flag4 = struct.unpack_from("<II", hdr, o)
            size, offset = struct.unpack_from("<QQ", hdr, o + 8)
            name_rel = struct.unpack_from("<I", hdr, o + 24)[0]
            s = blob_off + name_rel
            e = hdr.index(b"\0", s)
            name = hdr[s:e].decode("ascii", "replace")
            key = bytes(hdr[key_off + 16 * i:key_off + 16 * i + 16])
            ent = VbfEntry(i, name, key, first_chunk, size, offset, flag4)
            self.entries.append(ent)
            self.by_name[name] = ent
            self.by_key[key] = ent
        self._header = hdr

    def close(self):
        self._f.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def verify_header_md5(self):
        return hashlib.md5(self._header).digest() == self.stored_md5

    @staticmethod
    def key_for(path):
        """The lookup key the game computes: md5 of the lowercased path, forward slashes,
        no leading slash and no archive root prefix."""
        p = path.replace("\\", "/").lower().lstrip("/")
        return hashlib.md5(p.encode("ascii")).digest()

    def lookup(self, path):
        p = path.replace("\\", "/").lower().lstrip("/")
        ent = self.by_name.get(p)
        if ent is None:
            ent = self.by_key.get(self.key_for(p))
        return ent

    def read(self, path_or_entry):
        ent = path_or_entry
        if not isinstance(ent, VbfEntry):
            ent = self.lookup(ent)
            if ent is None:
                raise KeyError(path_or_entry)
        if ent.size == 0:
            return b""

        n = ent.chunk_count
        last = n - 1
        tail = ent.size & 0xFFFF            # bytes in the final chunk, 0 means a full 64 KiB
        self._f.seek(ent.offset)

        out = []
        for j in range(n):
            stored = self._chunk_sizes[ent.first_chunk + j]
            if stored == 0:
                # A stored size of zero means the chunk was kept raw at the full 64 KiB.
                out.append(self._f.read(CHUNK))
                continue
            raw = self._f.read(stored)
            if len(raw) != stored:
                raise IOError("short read on chunk %d of %s" % (j, ent.name))
            if j == last and stored == tail:
                # The compressor gave up on the tail and stored it verbatim. This is the
                # same test FFX_VbfArchive__open uses to clear the per chunk compressed flag.
                out.append(raw)
                continue
            try:
                out.append(zlib.decompress(raw))
            except zlib.error:
                # Fall back to a raw deflate stream with no zlib wrapper.
                out.append(zlib.decompress(raw, -15))
        data = b"".join(out)
        if len(data) != ent.size:
            raise IOError("%s decompressed to %d bytes, header says %d"
                          % (ent.name, len(data), ent.size))
        return data


def cmd_info(a, ar):
    print("archive      %s" % ar.path)
    print("file size    %d (%.2f GiB)" % (ar.file_size, ar.file_size / 1024.0 ** 3))
    print("header size  %d" % ar.header_size)
    print("header md5   %s  (%s)" % (ar.stored_md5.hex(),
                                     "ok" if ar.verify_header_md5() else "MISMATCH"))
    print("entries      %d" % len(ar.entries))
    print("chunk table  %d entries at 0x%X" % (ar.chunk_count, ar.chunk_table_offset))
    tot = sum(e.size for e in ar.entries)
    print("uncompressed %d (%.2f GiB)" % (tot, tot / 1024.0 ** 3))


def cmd_list(a, ar):
    lines = sorted(e.name for e in ar.entries)
    if a.out:
        with open(a.out, "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
        print("wrote %d names to %s" % (len(lines), a.out))
    else:
        for n in lines:
            print(n)


def cmd_find(a, ar):
    pat = a.pattern.lower()
    hits = [e for e in ar.entries if pat in e.name]
    hits.sort(key=lambda e: e.name)
    for e in hits:
        print("%10d  %s" % (e.size, e.name))
    print("%d match(es)" % len(hits), file=sys.stderr)


def cmd_cat(a, ar):
    data = ar.read(a.path)
    out = getattr(sys.stdout, "buffer", sys.stdout)
    out.write(data)


def cmd_extract(a, ar):
    ent = ar.lookup(a.path)
    if ent is None:
        sys.exit("not in archive: %s" % a.path)
    data = ar.read(ent)
    dest = a.out or os.path.basename(ent.name)
    d = os.path.dirname(dest)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(dest, "wb") as fh:
        fh.write(data)
    print("%s -> %s (%d bytes)" % (ent.name, dest, len(data)))


def cmd_extract_all(a, ar):
    pat = a.pattern.lower()
    hits = [e for e in ar.entries if pat in e.name]
    if not hits:
        sys.exit("nothing matches %r" % a.pattern)
    done = 0
    for e in hits:
        dest = os.path.join(a.out, e.name.replace("/", os.sep))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as fh:
            fh.write(ar.read(e))
        done += 1
        if done % 200 == 0:
            print("  %d/%d" % (done, len(hits)), file=sys.stderr)
    print("extracted %d file(s) under %s" % (done, a.out))


def main(argv=None):
    p = argparse.ArgumentParser(description="Read the FFX HD Remaster VBF archives.")
    p.add_argument("--vbf", default=DEFAULT_VBF,
                   help="path to the .vbf (default: the Steam FFX one)")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info").set_defaults(fn=cmd_info)

    sp = sub.add_parser("list")
    sp.add_argument("--out")
    sp.set_defaults(fn=cmd_list)

    sp = sub.add_parser("find")
    sp.add_argument("pattern")
    sp.set_defaults(fn=cmd_find)

    sp = sub.add_parser("cat")
    sp.add_argument("path")
    sp.set_defaults(fn=cmd_cat)

    sp = sub.add_parser("extract")
    sp.add_argument("path")
    sp.add_argument("-o", "--out")
    sp.set_defaults(fn=cmd_extract)

    sp = sub.add_parser("extract-all")
    sp.add_argument("pattern")
    sp.add_argument("-o", "--out", required=True)
    sp.set_defaults(fn=cmd_extract_all)

    a = p.parse_args(argv)
    with VbfArchive(a.vbf) as ar:
        a.fn(a, ar)


if __name__ == "__main__":
    main()
