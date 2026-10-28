#!/usr/bin/env python3
"""
fsb.py - the FSB5 sound banks of FINAL FANTASY X HD Remaster, read two ways.

Every bank lives under ffx_data/gamedata/ps3data/sound_pc/ in FFX_Data.vbf. This tool reads
each one in memory through vbf.py and lists its subsounds, either through FMOD Core (the
64-bit fmod.dll of the installed FMOD Studio API, loaded with ctypes) or through its own FSB5
header parser. Nothing is written to disk, and the listings are printed, never saved, because
the sound names are game data.

Usage
  python -I fsb.py banks   [filter] [--via core|header]  one line per bank
  python -I fsb.py names   [filter] [--via core|header]  every subsound name, per bank
  python -I fsb.py summary [filter]                      counts, versions, codecs (header parse)
  python -I fsb.py check   [filter]                      FMOD Core against the header parse
  python -I fsb.py refuse  [fev]                         FMOD Studio and FMOD Core on a .fev

[filter] is a substring of the archive path: sfx/us, voice/jp, music_ps2, 0201_bank00.
--via defaults to core for banks and names. --sdk points at another FMOD Studio API install.

FMOD Core opens each bank with FMOD_OPENMEMORY | FMOD_CREATECOMPRESSEDSAMPLE and no
FMOD_NONBLOCKING, on an FMOD_OUTPUTTYPE_NOSOUND system, so no audio device is touched and
nothing is decoded. FMOD copies the buffer, so the 672 MB PS2 music bank peaks at about twice
that in memory.

=============================================================================
FSB5 AS SHIPPED  (every field below is checked by `check` against FMOD Core)
=============================================================================
  +0x00 char[4] "FSB5"
  +0x04 u32     version. 1 in all 1,260 banks, which makes the header 0x3C bytes
  +0x08 u32     subsound count
  +0x0C u32     size of the subsound header block
  +0x10 u32     size of the name table
  +0x14 u32     size of the sample data
  +0x18 u32     codec, FSB5 numbering (7 = IMA ADPCM, the only one shipped)
  +0x1C u32[2]  zero
  +0x24 u8[8]   bank hash. The .fev wave bank record carries the same 8 bytes
  +0x2C u8[16]  more hash or GUID, not read by anything here
  +0x3C         subsound headers, then the name table, then the data
  Subsound header, a u64 then optional chunks:
    bit 0       another chunk follows
    bits 1-4    rate index: 0 4000, 1 8000, 2 11000, 3 11025, 4 16000, 5 22050,
                6 24000, 7 32000, 8 44100, 9 48000, 10 96000
    bits 5-6    channels: 0 mono, 1 stereo, 2 six, 3 eight
    bits 7-33   data offset / 32, from the start of the data block
    bits 34-63  length in samples
  Chunk, a u32: bit 0 more follow, bits 1-24 size, bits 25-31 type, then the body.
    type 1 channels (u8), 2 rate (u32), 3 loop (u32 start, u32 end, in samples),
    other types are kept as raw bytes.
  Name table: u32 offset per subsound (from the table start), then NUL-terminated names.
"""

import argparse
import ctypes
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import vbf  # noqa: E402

SOUND_ROOT = "ffx_data/gamedata/ps3data/sound_pc/"
DEFAULT_SDK = r"C:\Program Files (x86)\FMOD SoundSystem\FMOD Studio API Windows\api"

FSB5_CODECS = {
    0: "none", 1: "pcm8", 2: "pcm16", 3: "pcm24", 4: "pcm32", 5: "pcmfloat", 6: "gcadpcm",
    7: "imaadpcm", 8: "vag", 9: "hevag", 10: "xma", 11: "mpeg", 12: "celt", 13: "at9",
    14: "xwma", 15: "vorbis", 16: "fadpcm", 17: "opus",
}
FSB5_RATES = [4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000]
FSB5_CHANNELS = [1, 2, 6, 8]

# ------------------------------------------------------------------ header parse ----


class FsbError(Exception):
    pass


class Subsound(object):
    __slots__ = ("index", "name", "rate", "channels", "data_offset", "samples",
                 "loop", "chunks", "data_size")

    def __init__(self, index):
        self.index = index
        self.name = None
        self.rate = None
        self.channels = None
        self.data_offset = None
        self.samples = None
        self.loop = None
        self.chunks = []
        self.data_size = None

    @property
    def length_ms(self):
        return self.samples * 1000 // self.rate if self.rate else 0


class Fsb5(object):
    """Header-only parse of one FSB5 bank held in memory."""

    HEADER_V1 = 0x3C

    def __init__(self, data):
        if len(data) < self.HEADER_V1 or data[:4] != b"FSB5":
            raise FsbError("not an FSB5 bank (magic %r)" % bytes(data[:4]))
        (self.version, self.count, self.shdr_size, self.name_size, self.data_size,
         self.codec) = struct.unpack_from("<6I", data, 4)
        if self.version != 1:
            raise FsbError("FSB5 version %d, only version 1 is understood" % self.version)
        self.hash = bytes(data[0x24:0x2C])
        self.hash_tail = bytes(data[0x2C:0x3C])
        shdr = self.HEADER_V1
        names = shdr + self.shdr_size
        body = names + self.name_size
        if body + self.data_size != len(data):
            raise FsbError("sizes do not add up: header %d + data %d != file %d"
                           % (body, self.data_size, len(data)))
        self.data_start = body
        self.subsounds = []
        o = shdr
        for i in range(self.count):
            s = Subsound(i)
            mode = struct.unpack_from("<Q", data, o)[0]
            o += 8
            more = mode & 1
            s.rate = FSB5_RATES[(mode >> 1) & 0xF]
            s.channels = FSB5_CHANNELS[(mode >> 5) & 3]
            s.data_offset = ((mode >> 7) & 0x7FFFFFF) << 5
            s.samples = (mode >> 34) & 0x3FFFFFFF
            while more:
                c = struct.unpack_from("<I", data, o)[0]
                o += 4
                more = c & 1
                size = (c >> 1) & 0xFFFFFF
                ctype = (c >> 25) & 0x7F
                body_bytes = bytes(data[o:o + size])
                o += size
                if ctype == 1:
                    s.channels = body_bytes[0]
                elif ctype == 2:
                    s.rate = struct.unpack_from("<I", body_bytes)[0]
                elif ctype == 3:
                    s.loop = struct.unpack_from("<II", body_bytes)
                s.chunks.append((ctype, body_bytes))
            self.subsounds.append(s)
        if o != names:
            raise FsbError("subsound headers end at 0x%X, the name table starts at 0x%X" % (o, names))
        if self.name_size:
            offs = struct.unpack_from("<%dI" % self.count, data, names)
            for s, rel in zip(self.subsounds, offs):
                a = names + rel
                e = data.index(b"\0", a)
                s.name = bytes(data[a:e]).decode("ascii", "replace")
        for i, s in enumerate(self.subsounds):
            end = self.subsounds[i + 1].data_offset if i + 1 < self.count else self.data_size
            s.data_size = end - s.data_offset

    @property
    def codec_name(self):
        return FSB5_CODECS.get(self.codec, "codec%d" % self.codec)


# ------------------------------------------------------------------ FMOD Core -------

FMOD_VERSION = 0x00020315          # fmod_common.h of the 2.03.15 SDK this was written against
FMOD_OUTPUTTYPE_NOSOUND = 2
FMOD_OPENMEMORY = 0x00000800
FMOD_CREATECOMPRESSEDSAMPLE = 0x00000200
FMOD_TIMEUNIT_PCM = 0x00000002
FMOD_STUDIO_LOAD_MEMORY = 0

FMOD_RESULTS = [
    "FMOD_OK", "FMOD_ERR_BADCOMMAND", "FMOD_ERR_CHANNEL_ALLOC", "FMOD_ERR_CHANNEL_STOLEN",
    "FMOD_ERR_DMA", "FMOD_ERR_DSP_CONNECTION", "FMOD_ERR_DSP_DONTPROCESS", "FMOD_ERR_DSP_FORMAT",
    "FMOD_ERR_DSP_INUSE", "FMOD_ERR_DSP_NOTFOUND", "FMOD_ERR_DSP_RESERVED", "FMOD_ERR_DSP_SILENCE",
    "FMOD_ERR_DSP_TYPE", "FMOD_ERR_FILE_BAD", "FMOD_ERR_FILE_COULDNOTSEEK",
    "FMOD_ERR_FILE_DISKEJECTED", "FMOD_ERR_FILE_EOF", "FMOD_ERR_FILE_ENDOFDATA",
    "FMOD_ERR_FILE_NOTFOUND", "FMOD_ERR_FORMAT", "FMOD_ERR_HEADER_MISMATCH", "FMOD_ERR_HTTP",
    "FMOD_ERR_HTTP_ACCESS", "FMOD_ERR_HTTP_PROXY_AUTH", "FMOD_ERR_HTTP_SERVER_ERROR",
    "FMOD_ERR_HTTP_TIMEOUT", "FMOD_ERR_INITIALIZATION", "FMOD_ERR_INITIALIZED",
    "FMOD_ERR_INTERNAL", "FMOD_ERR_INVALID_FLOAT", "FMOD_ERR_INVALID_HANDLE",
    "FMOD_ERR_INVALID_PARAM", "FMOD_ERR_INVALID_POSITION", "FMOD_ERR_INVALID_SPEAKER",
    "FMOD_ERR_INVALID_SYNCPOINT", "FMOD_ERR_INVALID_THREAD", "FMOD_ERR_INVALID_VECTOR",
    "FMOD_ERR_MAXAUDIBLE", "FMOD_ERR_MEMORY", "FMOD_ERR_MEMORY_CANTPOINT", "FMOD_ERR_NEEDS3D",
    "FMOD_ERR_NEEDSHARDWARE", "FMOD_ERR_NET_CONNECT", "FMOD_ERR_NET_SOCKET_ERROR",
    "FMOD_ERR_NET_URL", "FMOD_ERR_NET_WOULD_BLOCK", "FMOD_ERR_NOTREADY",
    "FMOD_ERR_OUTPUT_ALLOCATED", "FMOD_ERR_OUTPUT_CREATEBUFFER", "FMOD_ERR_OUTPUT_DRIVERCALL",
    "FMOD_ERR_OUTPUT_FORMAT", "FMOD_ERR_OUTPUT_INIT", "FMOD_ERR_OUTPUT_NODRIVERS",
    "FMOD_ERR_PLUGIN", "FMOD_ERR_PLUGIN_MISSING", "FMOD_ERR_PLUGIN_RESOURCE",
    "FMOD_ERR_PLUGIN_VERSION", "FMOD_ERR_RECORD", "FMOD_ERR_REVERB_CHANNELGROUP",
    "FMOD_ERR_REVERB_INSTANCE", "FMOD_ERR_SUBSOUNDS", "FMOD_ERR_SUBSOUND_ALLOCATED",
    "FMOD_ERR_SUBSOUND_CANTMOVE", "FMOD_ERR_TAGNOTFOUND", "FMOD_ERR_TOOMANYCHANNELS",
    "FMOD_ERR_TRUNCATED", "FMOD_ERR_UNIMPLEMENTED", "FMOD_ERR_UNINITIALIZED",
    "FMOD_ERR_UNSUPPORTED", "FMOD_ERR_VERSION", "FMOD_ERR_EVENT_ALREADY_LOADED",
    "FMOD_ERR_EVENT_LIVEUPDATE_BUSY", "FMOD_ERR_EVENT_LIVEUPDATE_MISMATCH",
    "FMOD_ERR_EVENT_LIVEUPDATE_TIMEOUT", "FMOD_ERR_EVENT_NOTFOUND",
    "FMOD_ERR_STUDIO_UNINITIALIZED", "FMOD_ERR_STUDIO_NOT_LOADED", "FMOD_ERR_INVALID_STRING",
    "FMOD_ERR_ALREADY_LOCKED", "FMOD_ERR_NOT_LOCKED", "FMOD_ERR_RECORD_DISCONNECTED",
    "FMOD_ERR_TOOMANYSAMPLES",
]
FMOD_SOUND_TYPES = [
    "unknown", "aiff", "asf", "dls", "flac", "fsb", "it", "midi", "mod", "mpeg", "oggvorbis",
    "playlist", "raw", "s3m", "user", "wav", "xm", "xma", "audioqueue", "at9", "vorbis",
    "media_foundation", "mediacodec", "fadpcm", "opus",
]


def result_name(r):
    return "%d %s" % (r, FMOD_RESULTS[r] if 0 <= r < len(FMOD_RESULTS) else "?")


class CreateSoundExInfo(ctypes.Structure):
    """FMOD_CREATESOUNDEXINFO from the 2.03 fmod_common.h, field for field."""
    _fields_ = [
        ("cbsize", ctypes.c_int), ("length", ctypes.c_uint), ("fileoffset", ctypes.c_uint),
        ("numchannels", ctypes.c_int), ("defaultfrequency", ctypes.c_int),
        ("format", ctypes.c_int), ("decodebuffersize", ctypes.c_uint),
        ("initialsubsound", ctypes.c_int), ("numsubsounds", ctypes.c_int),
        ("inclusionlist", ctypes.c_void_p), ("inclusionlistnum", ctypes.c_int),
        ("pcmreadcallback", ctypes.c_void_p), ("pcmsetposcallback", ctypes.c_void_p),
        ("nonblockcallback", ctypes.c_void_p), ("dlsname", ctypes.c_char_p),
        ("encryptionkey", ctypes.c_char_p), ("maxpolyphony", ctypes.c_int),
        ("userdata", ctypes.c_void_p), ("suggestedsoundtype", ctypes.c_int),
        ("fileuseropen", ctypes.c_void_p), ("fileuserclose", ctypes.c_void_p),
        ("fileuserread", ctypes.c_void_p), ("fileuserseek", ctypes.c_void_p),
        ("fileuserasyncread", ctypes.c_void_p), ("fileuserasynccancel", ctypes.c_void_p),
        ("fileuserdata", ctypes.c_void_p), ("filebuffersize", ctypes.c_int),
        ("channelorder", ctypes.c_int), ("initialsoundgroup", ctypes.c_void_p),
        ("initialseekposition", ctypes.c_uint), ("initialseekpostype", ctypes.c_int),
        ("ignoresetfilesystem", ctypes.c_int), ("audioqueuepolicy", ctypes.c_uint),
        ("minmidigranularity", ctypes.c_uint), ("nonblockthreadid", ctypes.c_int),
        ("fsbguid", ctypes.c_void_p),
    ]


class FmodCore(object):
    """A NOSOUND FMOD Core system from the installed SDK's x64 fmod.dll."""

    def __init__(self, sdk=DEFAULT_SDK):
        lib = os.path.join(sdk, "core", "lib", "x64")
        if not os.path.isfile(os.path.join(lib, "fmod.dll")):
            raise FsbError("no fmod.dll under %s, pass --sdk" % lib)
        os.add_dll_directory(lib)
        self.dll = ctypes.WinDLL(os.path.join(lib, "fmod.dll"))
        ver = ctypes.c_uint()
        self.system = ctypes.c_void_p()
        self._ok(self.dll.FMOD_System_Create(ctypes.byref(self.system), FMOD_VERSION),
                 "System_Create")
        self._ok(self.dll.FMOD_System_GetVersion(self.system, ctypes.byref(ver), None),
                 "System_GetVersion")
        self.version = ver.value
        self._ok(self.dll.FMOD_System_SetOutput(self.system, FMOD_OUTPUTTYPE_NOSOUND),
                 "System_SetOutput")
        self._ok(self.dll.FMOD_System_Init(self.system, 32, 0, None), "System_Init")

    @staticmethod
    def _ok(r, what):
        if r != 0:
            raise FsbError("FMOD %s failed: %s" % (what, result_name(r)))

    def open_memory(self, data, mode=FMOD_OPENMEMORY | FMOD_CREATECOMPRESSEDSAMPLE):
        """createSound on bytes in memory. Returns (result, sound handle or None)."""
        ex = CreateSoundExInfo()
        ex.cbsize = ctypes.sizeof(CreateSoundExInfo)
        ex.length = len(data)
        snd = ctypes.c_void_p()
        r = self.dll.FMOD_System_CreateSound(self.system, ctypes.c_char_p(bytes(data)), mode,
                                             ctypes.byref(ex), ctypes.byref(snd))
        return r, (snd if r == 0 else None)

    def describe(self, data):
        """Open a bank and return a list of per-subsound dicts, as FMOD Core reports them."""
        r, snd = self.open_memory(data)
        if r != 0:
            raise FsbError("createSound refused the bank: %s" % result_name(r))
        try:
            n = ctypes.c_int()
            self._ok(self.dll.FMOD_Sound_GetNumSubSounds(snd, ctypes.byref(n)), "getNumSubSounds")
            out = []
            name = ctypes.create_string_buffer(512)
            for i in range(n.value):
                sub = ctypes.c_void_p()
                self._ok(self.dll.FMOD_Sound_GetSubSound(snd, i, ctypes.byref(sub)), "getSubSound")
                self._ok(self.dll.FMOD_Sound_GetName(sub, name, len(name)), "getName")
                length = ctypes.c_uint()
                self._ok(self.dll.FMOD_Sound_GetLength(sub, ctypes.byref(length),
                                                       FMOD_TIMEUNIT_PCM), "getLength")
                stype, fmt, ch, bits = ctypes.c_int(), ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
                self._ok(self.dll.FMOD_Sound_GetFormat(sub, ctypes.byref(stype), ctypes.byref(fmt),
                                                       ctypes.byref(ch), ctypes.byref(bits)),
                         "getFormat")
                freq, prio = ctypes.c_float(), ctypes.c_int()
                self._ok(self.dll.FMOD_Sound_GetDefaults(sub, ctypes.byref(freq),
                                                         ctypes.byref(prio)), "getDefaults")
                ls, le = ctypes.c_uint(), ctypes.c_uint()
                self._ok(self.dll.FMOD_Sound_GetLoopPoints(sub, ctypes.byref(ls), FMOD_TIMEUNIT_PCM,
                                                           ctypes.byref(le), FMOD_TIMEUNIT_PCM),
                         "getLoopPoints")
                out.append({
                    "index": i, "name": name.value.decode("ascii", "replace"),
                    "samples": length.value, "channels": ch.value, "bits": bits.value,
                    "rate": int(round(freq.value)), "type": stype.value,
                    "loop": (ls.value, le.value),
                })
            return out
        finally:
            self.dll.FMOD_Sound_Release(snd)

    def close(self):
        if self.system:
            self.dll.FMOD_System_Release(self.system)
            self.system = None


class FmodStudio(object):
    """An FMOD Studio system from the installed SDK, only used to show what it refuses."""

    def __init__(self, sdk=DEFAULT_SDK):
        core = os.path.join(sdk, "core", "lib", "x64")
        lib = os.path.join(sdk, "studio", "lib", "x64")
        os.add_dll_directory(core)
        os.add_dll_directory(lib)
        self.dll = ctypes.WinDLL(os.path.join(lib, "fmodstudio.dll"))
        self.system = ctypes.c_void_p()
        FmodCore._ok(self.dll.FMOD_Studio_System_Create(ctypes.byref(self.system), FMOD_VERSION),
                     "Studio_System_Create")
        core_sys = ctypes.c_void_p()
        FmodCore._ok(self.dll.FMOD_Studio_System_GetCoreSystem(self.system, ctypes.byref(core_sys)),
                     "Studio_System_GetCoreSystem")
        cdll = ctypes.WinDLL(os.path.join(core, "fmod.dll"))
        FmodCore._ok(cdll.FMOD_System_SetOutput(core_sys, FMOD_OUTPUTTYPE_NOSOUND), "SetOutput")
        FmodCore._ok(self.dll.FMOD_Studio_System_Initialize(self.system, 32, 0, 0, None),
                     "Studio_System_Initialize")

    def load_bank_memory(self, data):
        bank = ctypes.c_void_p()
        r = self.dll.FMOD_Studio_System_LoadBankMemory(self.system, ctypes.c_char_p(bytes(data)),
                                                       len(data), FMOD_STUDIO_LOAD_MEMORY, 0,
                                                       ctypes.byref(bank))
        if r == 0:
            self.dll.FMOD_Studio_Bank_Unload(bank)
        return r

    def close(self):
        if self.system:
            self.dll.FMOD_Studio_System_Release(self.system)
            self.system = None


# ------------------------------------------------------------------ archive ---------


def bank_entries(ar, flt=None):
    out = [e for e in ar.entries
           if e.name.startswith(SOUND_ROOT) and e.name.endswith(".fsb")]
    if flt:
        out = [e for e in out if flt.lower() in e.name]
    return sorted(out, key=lambda e: e.name)


def short(name):
    return name[len(SOUND_ROOT):] if name.startswith(SOUND_ROOT) else name


def group_of(name):
    s = short(name)
    return s.rsplit("/", 1)[0] if "/" in s else ""


# ------------------------------------------------------------------ commands --------


def cmd_banks(a, ar):
    core = FmodCore(a.sdk) if a.via == "core" else None
    total = 0
    for e in bank_entries(ar, a.filter):
        d = ar.read(e)
        h = Fsb5(d)
        n = len(core.describe(d)) if core else h.count
        total += n
        print("%-48s %5d subsounds  %-9s v%d  %10d bytes  hash %s"
              % (short(e.name), n, h.codec_name, h.version, len(d), h.hash.hex()))
    print("total subsounds %d (via %s)" % (total, a.via))


def cmd_names(a, ar):
    core = FmodCore(a.sdk) if a.via == "core" else None
    if core:
        print("# FMOD Core %x, fmod.dll from %s" % (core.version, a.sdk))
    for e in bank_entries(ar, a.filter):
        d = ar.read(e)
        if core:
            subs = core.describe(d)
            print("%s  %d" % (short(e.name), len(subs)))
            for s in subs:
                print("  %4d  %-40s %9d samples  %d ch  %5d Hz"
                      % (s["index"], s["name"], s["samples"], s["channels"], s["rate"]))
        else:
            h = Fsb5(d)
            print("%s  %d" % (short(e.name), h.count))
            for s in h.subsounds:
                loop = "  loop %d..%d" % s.loop if s.loop else ""
                print("  %4d  %-40s %9d samples  %d ch  %5d Hz%s"
                      % (s.index, s.name, s.samples, s.channels, s.rate, loop))


def cmd_summary(a, ar):
    from collections import Counter, defaultdict
    by_dir = defaultdict(lambda: [0, 0, 0])
    versions, codecs, rates, chans, chunk_types = Counter(), Counter(), Counter(), Counter(), Counter()
    loops = 0
    total_ms = defaultdict(int)
    for e in bank_entries(ar, a.filter):
        h = Fsb5(ar.read(e))
        g = group_of(e.name)
        by_dir[g][0] += 1
        by_dir[g][1] += h.count
        by_dir[g][2] += e.size
        versions[h.version] += 1
        codecs[h.codec_name] += 1
        for s in h.subsounds:
            rates[s.rate] += 1
            chans[s.channels] += 1
            total_ms[g] += s.length_ms
            if s.loop:
                loops += 1
            for t, _ in s.chunks:
                chunk_types[t] += 1
    print("%-12s %6s %10s %14s %12s" % ("dir", "banks", "subsounds", "bytes", "minutes"))
    for g in sorted(by_dir):
        b, n, size = by_dir[g]
        print("%-12s %6d %10d %14d %12.1f" % (g, b, n, size, total_ms[g] / 60000.0))
    print("banks %d, subsounds %d" % (sum(v[0] for v in by_dir.values()),
                                       sum(v[1] for v in by_dir.values())))
    print("versions", dict(versions))
    print("codecs  ", dict(codecs))
    print("rates   ", dict(sorted(rates.items())))
    print("channels", dict(sorted(chans.items())))
    print("chunks  ", dict(sorted(chunk_types.items())), " subsounds with a loop chunk:", loops)


def cmd_check(a, ar):
    core = FmodCore(a.sdk)
    print("# FMOD Core %x against the FSB5 header parse" % core.version)
    banks = subs = 0
    bad = []
    clamped = []
    for e in bank_entries(ar, a.filter):
        d = ar.read(e)
        h = Fsb5(d)
        c = core.describe(d)
        banks += 1
        subs += len(c)
        if len(c) != h.count:
            bad.append("%s: core %d subsounds, header %d" % (short(e.name), len(c), h.count))
            continue
        for s, k in zip(h.subsounds, c):
            diffs = []
            if s.name != k["name"]:
                diffs.append("name %r / %r" % (s.name, k["name"]))
            if s.samples != k["samples"]:
                diffs.append("samples %d / %d" % (s.samples, k["samples"]))
            if s.channels != k["channels"]:
                diffs.append("channels %d / %d" % (s.channels, k["channels"]))
            if s.rate != k["rate"]:
                diffs.append("rate %d / %d" % (s.rate, k["rate"]))
            if s.loop and tuple(s.loop) != tuple(k["loop"]):
                if (s.loop[0] == k["loop"][0] and s.loop[1] >= s.samples
                        and k["loop"][1] == s.samples - 1):
                    # The header's loop end runs past the subsound, FMOD clamps it.
                    clamped.append("%s #%d: loop end %d past %d samples, FMOD uses %d"
                                   % (short(e.name), s.index, s.loop[1], s.samples, k["loop"][1]))
                else:
                    diffs.append("loop %r / %r" % (s.loop, k["loop"]))
            if diffs:
                bad.append("%s #%d: %s" % (short(e.name), s.index, ", ".join(diffs)))
    print("banks %d, subsounds %d, disagreements %d, loop ends clamped by FMOD %d"
          % (banks, subs, len(bad), len(clamped)))
    for b in bad[:50]:
        print("  " + b)
    for b in clamped:
        print("  clamped " + b)
    return 1 if bad else 0


def cmd_refuse(a, ar):
    if a.fev:
        ent = ar.lookup(a.fev) or ar.lookup(SOUND_ROOT + a.fev)
    else:
        ent = ar.lookup(SOUND_ROOT + "sfx/us/0201.fev")
    if ent is None:
        raise FsbError("no such .fev: %s" % a.fev)
    d = ar.read(ent)
    print("file   %s, %d bytes, starts %r" % (short(ent.name), len(d), d[:16]))
    st = FmodStudio(a.sdk)
    r = st.load_bank_memory(d)
    print("FMOD Studio %x  Studio::System::loadBankMemory  -> %s" % (FMOD_VERSION, result_name(r)))
    st.close()
    core = FmodCore(a.sdk)
    r, snd = core.open_memory(d)
    if snd:
        core.dll.FMOD_Sound_Release(snd)
    print("FMOD Core   %x  System::createSound (OPENMEMORY)  -> %s" % (core.version, result_name(r)))
    core.close()
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description="List the FSB5 banks of FFX HD Remaster.")
    p.add_argument("--vbf", default=vbf.DEFAULT_VBF)
    p.add_argument("--sdk", default=DEFAULT_SDK, help="FMOD Studio API 'api' directory")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name, fn in [("banks", cmd_banks), ("names", cmd_names)]:
        sp = sub.add_parser(name)
        sp.add_argument("filter", nargs="?")
        sp.add_argument("--via", choices=["core", "header"], default="core")
        sp.set_defaults(fn=fn)
    for name, fn in [("summary", cmd_summary), ("check", cmd_check)]:
        sp = sub.add_parser(name)
        sp.add_argument("filter", nargs="?")
        sp.set_defaults(fn=fn)
    sp = sub.add_parser("refuse")
    sp.add_argument("fev", nargs="?")
    sp.set_defaults(fn=cmd_refuse)
    a = p.parse_args(argv)
    with vbf.VbfArchive(a.vbf) as ar:
        try:
            return a.fn(a, ar) or 0
        except FsbError as ex:
            print("error: %s" % ex, file=sys.stderr)
            return 2


if __name__ == "__main__":
    sys.exit(main())
