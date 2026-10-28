#!/usr/bin/env python3
"""
fev.py - reader for the FMOD Designer event projects (.fev) of FINAL FANTASY X HD Remaster.

The 1,260 projects under ffx_data/gamedata/ps3data/sound_pc/ in FFX_Data.vbf are read in
memory through vbf.py. Nothing is written to disk. Event and sound names are game data, so the
listings are printed on demand and never saved.

The format was taken from the shipped fmod_event.dll (FMOD Ex 4.44.32), whose loader reads it
field by field. Addresses below are VAs in that DLL at its preferred base 0x10000000.
reversing/SOUND_FORMATS.md has the full write-up.

Usage
  python -I fev.py projects [filter]           one line per project
  python -I fev.py events   [filter] [--names] every event: group path, type, parameters,
                                               and the wave bank and subsound it plays.
                                               --names also opens each .fsb for the real
                                               subsound name (slow for the music banks)
  python -I fev.py dump     <project>          one project in full
  python -I fev.py summary  [filter]           which Designer features the game uses
  python -I fev.py check    [filter]           parse everything, then cross-check every
                                               waveform against its FSB5 bank and every
                                               SFX side table against its project
  python -I fev.py seq      [filter]           SE sequence id -> project, group, event
  python -I fev.py bgm      [--ps2]            BGM track -> music event (reads FFX.exe)
  python -I fev.py voice    [--lang us|jp]     voice id -> project and event

[filter] is a substring of the archive path: sfx/us, voice/jp, music, 0201.

=============================================================================
CONTAINER  (RIFF, little endian, chunk bodies padded to even sizes)
=============================================================================
  RIFF 'FEV '
    'FMT '  u32 version, 0x00450000 in every shipped file. The loader at 0x10034260 reads
            the first dword and branches: 'FEV1' is the old flat format, 'RIFF' this one.
    LIST 'PROJ'  (chunk handlers matched through the table at 0x10050E30)
      'OBCT'  u32 n, then n x {u32 type, u32 count}: object counts for preallocation
      'PROP'  u32 len, char[len] project name
      'LGCY'  u32 pool size, u32 skipped, then the FEV1 body below   (0x10033BD0)
      'EPRP'  u32 n, then n x {u32, u32, u32}: envelope points        (0x10033AC0)
      'STRR'  u32 n, u32 offset[n], then the string blob              (0x10031560)
              Every group, event, parameter and sound definition name is an index here.
      'LANG'  u32 n, n x {u32 len, name}, u32 trailing                 (0x10031640)

=============================================================================
LGCY BODY  (0x10031FE0, every gate below version 0x450000 taken)
=============================================================================
  u32 len, name                     project name
  u32 nbanks, u32 nplatforms
  nbanks x  u32 mode, u32 max streams, nplatforms x {u8[8] fsb hash, u32},
            u32 len, name       the hash is the FSB5 header's +0x24
  category tree                     0x1000F270, recursive
            u32 len, name, f32 volume, f32 pitch, u32 max playbacks,
            u32 max playback behaviour, u32 nsub, nsub x category
  u32 nroot, nroot x group          0x100116A0, recursive
            u32 name index, u32 nprops, nprops x user property,
            u32 nsubgroups, u32 nevents, subgroups, then events
  event     u32 type (8 complex, 0x10 simple, factory 0x10031BD0), u32 name index,
            u8[16] GUID, a fixed block of 46 dwords (EVENT_FIELDS below),
            the type's body (vtable +132: complex 0x10021440, simple 0x10023BE0),
            u32 ncategories, ncategories x {u32 len, name}
  simple    u32, sound instance
  complex   u32 nlayers, nlayers x layer, u32 nparams, nparams x parameter,
            u32 nprops, nprops x user property
  layer     u16 flags, s16 priority, u16 controlling parameter (0xFFFF none),
            u16 ninstances, u16 nenvelopes, ninstances x sound instance,
            nenvelopes x envelope (0x10020DB0)
  sound instance (0x1002AE80, 58 bytes)
            u16 sound definition index, f32 start, f32 length, then 12 dwords
  envelope  u32 parameter index, u32 len, effect name, u32 dsp, u32 flags, u32 flags2,
            u32 npoints, npoints x u32 index into EPRP, u32 x2
  parameter u32 name index, f32 velocity, f32 min, f32 max, u32 flags, f32 seek speed,
            u32, u32 nsustain, nsustain x f32
  user property  u32 len, name, u32 type, then int (0), float (1) or {u32 len, string} (2)
  u32 nconfigs, nconfigs x sound definition config (vtable +52, 0x1002D8B0, 74 bytes)
  u32 nsounddefs, nsounddefs x sound definition
            u32 name index, u32 config index, u32 nwaves, nwaves x waveform
  waveform  u32 type, u32 weight, then by type
            0 wavetable  u32 len, source file name, u32 bank index, u32 subsound index,
                         u32 length in ms
            1 oscillator u32, u32
            2 null, 3 programmer: nothing more
  u32 nreverbs, nreverbs x {u32 len, name, 33 dwords}
  music     u32 size, u32 'comp', then the interactive music chunks when size > 8
"""

import argparse
import os
import struct
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import vbf  # noqa: E402

SOUND_ROOT = "ffx_data/gamedata/ps3data/sound_pc/"
FEV_VERSION = 0x00450000
EVENT_COMPLEX = 8
EVENT_SIMPLE = 0x10

# The fixed event block in read order. Names come from the EventI field each value lands in
# (0x100116A0) and from the FMOD Designer 4 event property it matches. The ones marked "?"
# are only placed, not understood.
EVENT_FIELDS = (
    [("volume", "f"), ("pitch", "f"), ("pitch_random", "f"), ("volume_random", "f"),
     ("priority", "I"), ("max_playbacks", "I"), ("steal_priority", "I"), ("mode", "I"),
     ("min_distance", "f"), ("max_distance", "f"), ("u300?", "I"), ("f304?", "f"),
     ("flags", "I")]
    + [("speaker%d" % i, "f") for i in range(8)]
    + [("cone_inside", "f"), ("cone_outside", "f"), ("cone_outside_volume", "f"),
       ("rolloff", "I"), ("doppler", "f"), ("reverb_dry", "f"), ("reverb_wet", "f"),
       ("speaker_spread", "f"), ("fade_in_ms", "I"), ("fade_out_ms", "I"),
       ("spawn_intensity", "f"), ("spawn_intensity_random", "f"), ("pan_level_3d", "f"),
       ("u260?", "I"), ("u264?", "I")]
)
SOUND_INSTANCE_DWORDS = ["u_a?", "loop_mode", "loop_count", "u_b?", "u40?", "u44?", "u48?",
                         "u28?", "u32?", "u36?", "u_c?", "u_d?"]
CONFIG_FIELDS = ["play_mode", "spawn_min", "spawn_max", "max_spawned", "volume?",
                 "vol_rand_mode?", "u20?", "u24?", "u28?", "u32?", "pitch_rand_mode?",
                 "u36?", "u40?", "u44?", "u_x3?", "u48?", "u52?"]
WAVE_TYPES = {0: "wavetable", 1: "oscillator", 2: "null", 3: "programmer"}


class FevError(Exception):
    pass


class Reader(object):
    def __init__(self, data, off=0, end=None):
        self.d = data
        self.o = off
        self.end = len(data) if end is None else end

    def need(self, n):
        if self.o + n > self.end:
            raise FevError("read of %d bytes at 0x%X runs past 0x%X" % (n, self.o, self.end))

    def u8(self):
        self.need(1)
        v = self.d[self.o]
        self.o += 1
        return v

    def u16(self):
        self.need(2)
        v = struct.unpack_from("<H", self.d, self.o)[0]
        self.o += 2
        return v

    def s16(self):
        self.need(2)
        v = struct.unpack_from("<h", self.d, self.o)[0]
        self.o += 2
        return v

    def u32(self):
        self.need(4)
        v = struct.unpack_from("<I", self.d, self.o)[0]
        self.o += 4
        return v

    def s32(self):
        self.need(4)
        v = struct.unpack_from("<i", self.d, self.o)[0]
        self.o += 4
        return v

    def f32(self):
        self.need(4)
        v = struct.unpack_from("<f", self.d, self.o)[0]
        self.o += 4
        return v

    def raw(self, n):
        self.need(n)
        v = bytes(self.d[self.o:self.o + n])
        self.o += n
        return v

    def lstr(self):
        """u32 length including the NUL, then the bytes."""
        n = self.u32()
        if n > 0x10000:
            raise FevError("string length %d at 0x%X" % (n, self.o - 4))
        b = self.raw(n)
        return b.split(b"\0", 1)[0].decode("latin1")


class Obj(object):
    def __init__(self, **kw):
        self.__dict__.update(kw)


class Project(object):
    """One parsed .fev."""

    def __init__(self, data, path=""):
        self.path = path
        self.size = len(data)
        self.strings = []
        self.points = []
        self.objcounts = []
        self.langs = []
        self.music_chunks = []
        self.lgcy = None
        if data[:4] != b"RIFF" or data[8:12] != b"FEV ":
            raise FevError("not a RIFF FEV project (%r)" % bytes(data[:12]))
        riff_end = 8 + struct.unpack_from("<I", data, 4)[0]
        if riff_end != len(data):
            raise FevError("RIFF size %d, file %d" % (riff_end, len(data)))
        self.chunks = []
        o = 12
        while o + 8 <= riff_end:
            cid = data[o:o + 4].decode("latin1")
            size = struct.unpack_from("<I", data, o + 4)[0]
            body = o + 8
            if cid == "FMT ":
                self.version = struct.unpack_from("<I", data, body)[0]
            elif cid == "LIST":
                form = data[body:body + 4]
                if form != b"PROJ":
                    raise FevError("LIST form %r" % form)
                self._proj(data, body + 4, body + size)
            self.chunks.append((cid, size))
            o = body + size + (size & 1)
        if o != riff_end:
            raise FevError("RIFF chunks end at 0x%X, not 0x%X" % (o, riff_end))
        if self.lgcy is None:
            raise FevError("no LGCY chunk")
        # names last, the string table comes after the body
        self._body(data, *self.lgcy)

    def s(self, idx):
        return self.strings[idx] if 0 <= idx < len(self.strings) else "<str %d>" % idx

    def _proj(self, data, o, end):
        while o + 8 <= end:
            cid = data[o:o + 4].decode("latin1")
            size = struct.unpack_from("<I", data, o + 4)[0]
            body = o + 8
            r = Reader(data, body, body + size)
            if cid == "OBCT":
                n = r.u32()
                self.objcounts = [(r.u32(), r.u32()) for _ in range(n)]
            elif cid == "PROP":
                self.prop_name = r.lstr()
            elif cid == "LGCY":
                self.lgcy = (body, body + size)
            elif cid == "EPRP":
                n = r.u32()
                self.points = [(r.u32(), r.u32(), r.u32()) for _ in range(n)]
            elif cid == "STRR":
                n = r.u32()
                offs = [r.u32() for _ in range(n)]
                blob = body + 4 + 4 * n
                for x in offs:
                    e = data.index(b"\0", blob + x)
                    self.strings.append(bytes(data[blob + x:e]).decode("latin1"))
                r.o = r.end
            elif cid == "LANG":
                n = r.u32()
                self.langs = [r.lstr() for _ in range(n)]
                self.lang_tail = r.u32()
            else:
                raise FevError("unknown PROJ chunk %r" % cid)
            if r.o != r.end and cid not in ("LGCY",):
                raise FevError("%s chunk read to 0x%X, ends 0x%X" % (cid, r.o, r.end))
            self.chunks.append((cid, size))
            o = body + size + (size & 1)

    # ---------------------------------------------------------------- body -------

    def _body(self, data, body, end):
        if self.version != FEV_VERSION:
            raise FevError("FMT version 0x%X, this reader is for 0x%X" % (self.version, FEV_VERSION))
        r = Reader(data, body, end)
        self.pool_size = r.u32()
        self.pool_skip = r.u32()
        self.name = r.lstr()
        nbanks = r.u32()
        nplat = r.u32()
        self.banks = []
        for _ in range(nbanks):
            b = Obj(mode=r.u32(), max_streams=r.u32(), platforms=[])
            for _ in range(nplat):
                b.platforms.append((r.raw(8), r.u32()))
            b.name = r.lstr()
            self.banks.append(b)
        self.categories = self._category(r, "")
        nroot = r.u32()
        self.groups = [self._group(r, "") for _ in range(nroot)]
        nconf = r.u32()
        self.configs = []
        for _ in range(nconf):
            vals = [r.u32() for _ in range(17)]
            words = [r.u16(), r.u16(), r.u16()]
            self.configs.append(Obj(fields=dict(zip(CONFIG_FIELDS, vals)), words=words))
        nsd = r.u32()
        self.sounddefs = []
        for _ in range(nsd):
            sd = Obj(name=self.s(r.u32()), config=r.u32(), waves=[])
            for _ in range(r.u32()):
                w = Obj(type=r.u32(), weight=r.u32())
                if w.type == 0:
                    w.file = r.lstr()
                    w.bank = r.u32()
                    w.index = r.u32()
                    w.length_ms = r.u32()
                elif w.type == 1:
                    w.osc = (r.u32(), r.u32())
                sd.waves.append(w)
            self.sounddefs.append(sd)
        self.reverbs = []
        for _ in range(r.u32()):
            name = r.lstr()
            self.reverbs.append(Obj(name=name, values=[r.u32() for _ in range(33)]))
        size = r.u32()
        tag = r.raw(4)
        if tag != b"comp":
            raise FevError("expected the 'comp' music chunk at 0x%X, got %r" % (r.o - 4, tag))
        self.music_size = size
        mend = r.o + size - 8
        while r.o < mend:
            csize = r.u32()
            ctag = r.raw(4).decode("latin1")
            self.music_chunks.append((ctag, csize))
            r.raw(csize - 8)
        if r.o != end:
            raise FevError("LGCY body read to 0x%X, chunk ends 0x%X" % (r.o, end))

    def _category(self, r, parent):
        name = r.lstr()
        c = Obj(name=name, path=(parent + "/" + name) if parent else name,
                volume=r.f32(), pitch=r.f32(), max_playbacks=r.u32(), behaviour=r.u32(), subs=[])
        for _ in range(r.u32()):
            c.subs.append(self._category(r, c.path))
        return c

    def _props(self, r):
        out = []
        for _ in range(r.u32()):
            name = r.lstr()
            t = r.u32()
            if t == 0:
                v = r.s32()
            elif t == 1:
                v = r.f32()
            elif t == 2:
                v = r.lstr()
            else:
                v = None
            out.append((name, t, v))
        return out

    def _group(self, r, parent):
        name = self.s(r.u32())
        g = Obj(name=name, path=(parent + "/" + name) if parent else name, props=self._props(r))
        nsub = r.u32()
        nev = r.u32()
        g.groups = [self._group(r, g.path) for _ in range(nsub)]
        g.events = [self._event(r, g, i) for i in range(nev)]
        return g

    def _instance(self, r):
        si = Obj(sounddef=r.u16(), start=r.f32(), length=r.f32())
        si.fields = dict(zip(SOUND_INSTANCE_DWORDS, [r.u32() for _ in SOUND_INSTANCE_DWORDS]))
        return si

    def _event(self, r, group, index):
        ev = Obj(type=r.u32(), group=group, index=index)
        ev.name = self.s(r.u32())
        ev.guid = r.raw(16)
        ev.fields = {}
        for name, kind in EVENT_FIELDS:
            ev.fields[name] = r.f32() if kind == "f" else r.u32()
        ev.layers, ev.params, ev.props, ev.instances = [], [], [], []
        if ev.type == EVENT_COMPLEX:
            for _ in range(r.u32()):
                ly = Obj(flags=r.u16(), priority=r.s16(), param=r.u16())
                ninst = r.u16()
                nenv = r.u16()
                ly.instances = [self._instance(r) for _ in range(ninst)]
                ly.envelopes = []
                for _ in range(nenv):
                    en = Obj(param=r.s32(), effect=r.lstr(), dsp=r.u32(), flags=r.u32(),
                             flags2=r.u32())
                    en.points = [r.u32() for _ in range(r.u32())]
                    en.tail = (r.u32(), r.u32())
                    ly.envelopes.append(en)
                ev.layers.append(ly)
                ev.instances.extend(ly.instances)
            for _ in range(r.u32()):
                pm = Obj(name=self.s(r.u32()), velocity=r.f32(), min=r.f32(), max=r.f32(),
                         flags=r.u32(), seek=r.f32(), u44=r.u32())
                pm.sustain = [r.f32() for _ in range(r.u32())]
                ev.params.append(pm)
            ev.props = self._props(r)
        elif ev.type == EVENT_SIMPLE:
            ev.simple_word = r.u32()
            ev.instances = [self._instance(r)]
        else:
            raise FevError("event type %d at 0x%X" % (ev.type, r.o))
        ev.categories = [r.lstr() for _ in range(r.u32())]
        return ev

    # ---------------------------------------------------------------- walks ------

    def all_groups(self):
        out = []

        def walk(g):
            out.append(g)
            for s in g.groups:
                walk(s)
        for g in self.groups:
            walk(g)
        return out

    def all_events(self):
        return [ev for g in self.all_groups() for ev in g.events]

    def waves_of(self, ev):
        """(instance, sounddef, waveform) for everything an event can play."""
        out = []
        for si in ev.instances:
            sd = self.sounddefs[si.sounddef] if si.sounddef < len(self.sounddefs) else None
            if sd is None:
                out.append((si, None, None))
                continue
            for w in sd.waves:
                out.append((si, sd, w))
        return out


# ------------------------------------------------------------------ side tables -----


def parse_common(data):
    """<project>_common.txt: {u32 sequence, u32 event index} into group 'nonloop'
    (sub_710370, looked up by sub_710290)."""
    return [struct.unpack_from("<II", data, 8 * i) for i in range(len(data) // 8)]


def parse_loop(data):
    """<project>_loop.txt: {u32 sequence, u32 n, u32 index[n]} into group 'loop'
    (sub_710C50, looked up by sub_710AE0)."""
    out = []
    w = struct.unpack_from("<%dI" % (len(data) // 4), data)
    i = 0
    while i + 2 <= len(w):
        seq, n = w[i], w[i + 1]
        i += 2
        out.append((seq, list(w[i:i + n])))
        i += n
    return out


def parse_ml(data):
    """'ML\\0\\0', u32 count, count x {u32 key, u32 offset of a NUL-terminated name}.
    VoiceIDMapper.txt (voice id -> event or subsound name) and VoiceFevMapper.txt
    (map id -> voice project), read by FmodVoice__prepare and FmodVoice__dataChange."""
    if data[:4] != b"ML\0\0":
        raise FevError("not an ML table")
    n = struct.unpack_from("<I", data, 4)[0]
    out = []
    for i in range(n):
        key, off = struct.unpack_from("<II", data, 8 + 8 * i)
        e = data.index(b"\0", off)
        out.append((key, bytes(data[off:e]).decode("latin1")))
    return out


def voice_slot(name):
    """The project slot FmodVoice__prepare 0x70ADD0 computes from the first two characters of
    an event name, and FmodVoice__dataChange 0x70A630 from the last two of a project name:
    c1 + 2 * (5 * c0 - 264), which is the two digit number when both are digits."""
    return ord(name[1]) + 2 * (5 * ord(name[0]) - 264)


# FFX.exe, the BGM track -> music event index table g_ffxFmodBgmEventIndex, 182 dwords,
# read by FmodMusic__loadTrack 0x709080 through sub_708BE0. VA at the preferred base.
BGM_TABLE_VA = 0xB4F1C8
BGM_TRACKS = 182
BATTLE_TRACK, BATTLE_EVENT = 16, 6      # FmodMusic__loadBattleTrack 0x708A00
DEFAULT_EXE = os.path.join(os.path.dirname(os.path.dirname(vbf.DEFAULT_VBF)), "FFX.exe")


def read_bgm_table(exe):
    with open(exe, "rb") as f:
        pe = f.read()
    lfanew = struct.unpack_from("<I", pe, 0x3C)[0]
    nsec = struct.unpack_from("<H", pe, lfanew + 6)[0]
    optsize = struct.unpack_from("<H", pe, lfanew + 20)[0]
    base = struct.unpack_from("<I", pe, lfanew + 24 + 28)[0]
    rva = BGM_TABLE_VA - base
    sec = lfanew + 24 + optsize
    for i in range(nsec):
        s = sec + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", pe, s + 8)
        if va <= rva < va + max(vsize, rsize):
            o = raw + rva - va
            return list(struct.unpack_from("<%di" % BGM_TRACKS, pe, o))
    raise FevError("VA 0x%X is in no section of %s" % (BGM_TABLE_VA, exe))


# ------------------------------------------------------------------ archive ---------


def fev_entries(ar, flt=None):
    out = [e for e in ar.entries if e.name.startswith(SOUND_ROOT) and e.name.endswith(".fev")]
    if flt:
        out = [e for e in out if flt.lower() in e.name]
    return sorted(out, key=lambda e: e.name)


def short(name):
    return name[len(SOUND_ROOT):] if name.startswith(SOUND_ROOT) else name


def load(ar, ent):
    return Project(ar.read(ent), ent.name)


def bank_path(proj_entry_name, bank_name):
    return proj_entry_name.rsplit("/", 1)[0] + "/" + bank_name.lower() + ".fsb"


def find_project(ar, ref):
    ref = ref.lower()
    for e in fev_entries(ar):
        s = short(e.name)
        if s == ref or s.endswith("/" + ref) or s.endswith("/" + ref + ".fev"):
            return e
    raise FevError("no project matches %r" % ref)


# ------------------------------------------------------------------ commands --------


def describe_wave(p, w, names=None):
    if w is None:
        return "-"
    if w.type != 0:
        return WAVE_TYPES.get(w.type, "type%d" % w.type)
    bank = p.banks[w.bank].name if w.bank < len(p.banks) else "<bank %d>" % w.bank
    s = "%s #%d (%s, %d ms)" % (bank, w.index, w.file, w.length_ms)
    if names is not None:
        lst = names.get(w.bank)
        s += " = %s" % (lst[w.index] if lst and w.index < len(lst) else "<missing>")
    return s


def cmd_projects(a, ar):
    tot = Counter()
    for e in fev_entries(ar, a.filter):
        p = load(ar, e)
        evs = p.all_events()
        nc = sum(1 for ev in evs if ev.type == EVENT_COMPLEX)
        tot["projects"] += 1
        tot["events"] += len(evs)
        tot["complex"] += nc
        tot["sounddefs"] += len(p.sounddefs)
        print("%-40s %5d events (%d complex)  %3d groups  %5d sounddefs  banks %s"
              % (short(e.name), len(evs), nc, len(p.all_groups()), len(p.sounddefs),
                 ",".join(b.name for b in p.banks)))
    print("projects %(projects)d, events %(events)d, complex %(complex)d, "
          "sound definitions %(sounddefs)d" % tot)


def cmd_events(a, ar):
    if a.names:
        import fsb
    for e in fev_entries(ar, a.filter):
        p = load(ar, e)
        names = None
        if a.names:
            names = {}
            for i, b in enumerate(p.banks):
                bent = ar.lookup(bank_path(e.name, b.name))
                names[i] = [s.name for s in fsb.Fsb5(ar.read(bent)).subsounds] if bent else None
        print("%s  (%s)" % (short(e.name), p.name))
        for ev in p.all_events():
            kind = "complex" if ev.type == EVENT_COMPLEX else "simple"
            params = ", ".join("%s[%g..%g]" % (pm.name, pm.min, pm.max) for pm in ev.params)
            print("  %s/%s  #%d %s%s" % (ev.group.path, ev.name, ev.index, kind,
                                         ("  params: " + params) if params else ""))
            for si, sd, w in p.waves_of(ev):
                print("      sd %-4d %-44s %s" % (si.sounddef, sd.name if sd else "<none>",
                                                  describe_wave(p, w, names)))


def cmd_dump(a, ar):
    e = find_project(ar, a.project)
    p = load(ar, e)
    print("%s  %d bytes  FMT 0x%X  name %r (PROP %r)" % (short(e.name), p.size, p.version, p.name,
                                                        getattr(p, "prop_name", None)))
    print("chunks", p.chunks)
    print("objcounts", p.objcounts)
    print("pool %d skip %d  strings %d  envelope points %d  langs %s"
          % (p.pool_size, p.pool_skip, len(p.strings), len(p.points), p.langs))
    for b in p.banks:
        print("bank %r mode 0x%X max streams %d %s" % (b.name, b.mode, b.max_streams,
                                                       [(h.hex(), x) for h, x in b.platforms]))

    def cat(c, d):
        print("  " * d + "category %s vol %g pitch %g maxpb %d behaviour %d"
              % (c.name, c.volume, c.pitch, c.max_playbacks, c.behaviour))
        for s in c.subs:
            cat(s, d + 1)
    cat(p.categories, 0)
    for g in p.all_groups():
        print("group %s  %d events  props %s" % (g.path, len(g.events), g.props))
        for ev in g.events:
            print("  event #%d %s type %d guid %s categories %s" % (ev.index, ev.name, ev.type,
                                                                   ev.guid.hex(), ev.categories))
            print("    " + " ".join("%s=%s" % (k, ("%g" % v) if isinstance(v, float) else hex(v))
                                    for k, v in ev.fields.items()))
            for ly in ev.layers:
                print("    layer flags 0x%X prio %d param %d" % (ly.flags, ly.priority, ly.param))
                for en in ly.envelopes:
                    print("      envelope param %d %r dsp %d flags 0x%X 0x%X points %s"
                          % (en.param, en.effect, en.dsp, en.flags, en.flags2,
                             [p.points[i] if i < len(p.points) else i for i in en.points]))
            for pm in ev.params:
                print("    param %s vel %g range %g..%g flags 0x%X seek %g sustain %s"
                      % (pm.name, pm.velocity, pm.min, pm.max, pm.flags, pm.seek, pm.sustain))
            for si in ev.instances:
                print("    instance sd %d start %g length %g %s" % (si.sounddef, si.start,
                                                                    si.length, si.fields))
    for i, c in enumerate(p.configs):
        print("config %d %s words %s" % (i, c.fields, c.words))
    for i, sd in enumerate(p.sounddefs):
        print("sounddef %d %s config %d" % (i, sd.name, sd.config))
        for w in sd.waves:
            print("    %s weight %d %s" % (WAVE_TYPES.get(w.type, w.type), w.weight,
                                           describe_wave(p, w)))
    for rv in p.reverbs:
        print("reverb %s" % rv.name)
    print("music 'comp' size %d chunks %s" % (p.music_size, p.music_chunks))


def cmd_summary(a, ar):
    c = Counter()
    feats = defaultdict(Counter)
    for e in fev_entries(ar, a.filter):
        p = load(ar, e)
        c["projects"] += 1
        c["banks"] += len(p.banks)
        c["sounddefs"] += len(p.sounddefs)
        c["configs"] += len(p.configs)
        c["reverbs"] += len(p.reverbs)
        c["strings"] += len(p.strings)
        c["envelope points"] += len(p.points)
        if p.music_size > 8:
            c["projects with music data"] += 1
        feats["bank mode"][hex(p.banks[0].mode) if p.banks else "-"] += 1
        feats["bank max streams"][p.banks[0].max_streams if p.banks else "-"] += 1
        feats["languages"][",".join(p.langs)] += 1

        def cats(cg):
            feats["category"][cg.path] += 1
            for s in cg.subs:
                cats(s)
        cats(p.categories)
        for g in p.all_groups():
            c["groups"] += 1
            feats["group name"][g.path] += 1
            if g.props:
                c["groups with user properties"] += 1
        for ev in p.all_events():
            c["events"] += 1
            c["complex events" if ev.type == EVENT_COMPLEX else "simple events"] += 1
            f = ev.fields
            feats["event mode"][hex(f["mode"])] += 1
            feats["event 2D/3D"]["3D" if f["mode"] & 0x10 else "2D" if f["mode"] & 8 else "?"] += 1
            feats["event max playbacks"][f["max_playbacks"]] += 1
            feats["event volume"][round(f["volume"], 3)] += 1
            feats["event pitch"][round(f["pitch"], 3)] += 1
            feats["event fade in/out ms"][(f["fade_in_ms"], f["fade_out_ms"])] += 1
            feats["event categories"][",".join(ev.categories)] += 1
            feats["event reverb wet/dry"][(round(f["reverb_wet"], 3), round(f["reverb_dry"], 3))] += 1
            feats["event spawn intensity"][round(f["spawn_intensity"], 3)] += 1
            if f["pitch_random"] or f["volume_random"]:
                c["events with pitch or volume randomisation"] += 1
            c["layers"] += len(ev.layers)
            c["parameters"] += len(ev.params)
            c["event user properties"] += len(ev.props)
            for pm in ev.params:
                feats["parameter name"][pm.name] += 1
            for ly in ev.layers:
                for en in ly.envelopes:
                    c["envelopes"] += 1
                    feats["envelope effect"][en.effect] += 1
            for si in ev.instances:
                c["sound instances"] += 1
                feats["instance loop mode"][si.fields["loop_mode"]] += 1
                feats["instance loop count"][si.fields["loop_count"] if si.fields["loop_count"] < 0x80000000 else -1] += 1
        for sd in p.sounddefs:
            feats["waveforms per sounddef"][len(sd.waves)] += 1
            for w in sd.waves:
                feats["waveform type"][WAVE_TYPES.get(w.type, w.type)] += 1
        for cf in p.configs:
            feats["config play mode"][cf.fields["play_mode"]] += 1
            feats["config spawn min/max"][(cf.fields["spawn_min"], cf.fields["spawn_max"])] += 1
    for k in sorted(c):
        print("%-44s %d" % (k, c[k]))
    for k in sorted(feats):
        items = feats[k].most_common()
        shown = ", ".join("%s: %d" % (x, n) for x, n in items[:12])
        more = "  (+%d more values)" % (len(items) - 12) if len(items) > 12 else ""
        print("%-24s %s%s" % (k, shown, more))


def cmd_check(a, ar):
    import fsb
    c = Counter()
    bad = []
    fsb_cache = {}

    def bank_info(path):
        if path not in fsb_cache:
            ent = ar.lookup(path)
            fsb_cache.clear()           # keep one bank at a time, the music banks are big
            fsb_cache[path] = fsb.Fsb5(ar.read(ent)) if ent else None
        return fsb_cache[path]

    for e in fev_entries(ar, a.filter):
        try:
            p = load(ar, e)
        except FevError as ex:
            bad.append("%s: parse: %s" % (short(e.name), ex))
            continue
        c["projects parsed"] += 1
        c["events"] += len(p.all_events())
        if p.name != getattr(p, "prop_name", p.name):
            bad.append("%s: LGCY name %r, PROP name %r" % (short(e.name), p.name, p.prop_name))
        for bi, b in enumerate(p.banks):
            path = bank_path(e.name, b.name)
            h = bank_info(path)
            if h is None:
                bad.append("%s: bank %s missing" % (short(e.name), path))
                continue
            c["banks"] += 1
            if b.platforms and b.platforms[0][0] != h.hash:
                bad.append("%s: bank %s hash %s, fsb %s" % (short(e.name), b.name,
                                                            b.platforms[0][0].hex(), h.hash.hex()))
            used = set()
            for sd in p.sounddefs:
                for w in sd.waves:
                    if w.type != 0 or w.bank != bi:
                        continue
                    c["waveforms"] += 1
                    used.add(w.index)
                    if w.index >= h.count:
                        bad.append("%s: %s index %d of %d" % (short(e.name), w.file, w.index, h.count))
                        continue
                    s = h.subsounds[w.index]
                    stem = w.file.replace("\\", "/").rsplit("/", 1)[-1].rsplit(".", 1)[0]
                    if stem.lower() != (s.name or "").lower():
                        c["waveform name differs from subsound name"] += 1
                        if c["waveform name differs from subsound name"] <= 5:
                            bad.append("%s: %s -> #%d %r" % (short(e.name), w.file, w.index, s.name))
                    if abs(w.length_ms - s.length_ms) > 1:
                        c["waveform length differs by more than 1 ms"] += 1
            c["subsounds never referenced"] += h.count - len(used)
        # SFX side tables
        stem = e.name[:-4]
        groups = {g.path: g for g in p.all_groups()}
        for suffix, grp, parser in (("_common.txt", "nonloop", parse_common),
                                    ("_loop.txt", "loop", parse_loop)):
            te = ar.lookup(stem + suffix)
            if te is None:
                continue
            c["side tables"] += 1
            g = groups.get(grp)
            n = len(g.events) if g else 0
            for row in parser(ar.read(te)):
                idxs = row[1] if isinstance(row[1], list) else [row[1]]
                c["side table rows"] += 1
                for ix in idxs:
                    if ix >= n:
                        bad.append("%s%s: sequence %d -> %s #%d of %d"
                                   % (short(stem), suffix, row[0], grp, ix, n))
    for k in sorted(c):
        print("%-44s %d" % (k, c[k]))
    print("problems %d" % len(bad))
    for b in bad[:60]:
        print("  " + b)
    return 1 if any(": parse:" in b for b in bad) else 0


def cmd_seq(a, ar):
    seqs = defaultdict(list)
    for e in fev_entries(ar, a.filter or "sfx/"):
        stem = e.name[:-4]
        tables = [(ar.lookup(stem + "_common.txt"), "nonloop", parse_common),
                  (ar.lookup(stem + "_loop.txt"), "loop", parse_loop)]
        if not any(t for t, _, _ in tables):
            continue
        p = load(ar, e)
        groups = {g.path: g for g in p.all_groups()}
        for te, grp, parser in tables:
            if te is None:
                continue
            g = groups.get(grp)
            for row in parser(ar.read(te)):
                idxs = row[1] if isinstance(row[1], list) else [row[1]]
                evn = [g.events[i].name if g and i < len(g.events) else "<%d>" % i for i in idxs]
                seqs[row[0]].append((short(e.name), grp, idxs, evn))
    for s in sorted(seqs):
        for proj, grp, idxs, evn in seqs[s]:
            print("%6d  %-18s %-8s %s" % (s, proj, grp, ", ".join("#%d %s" % x for x in zip(idxs, evn))))
    multi = sum(1 for v in seqs.values() if len({x[0] for x in v}) > 1)
    print("sequence ids %d, in more than one project %d" % (len(seqs), multi))


def cmd_bgm(a, ar):
    table = read_bgm_table(a.exe)
    path = "music_ps2/ffx_music_ps2.fev" if a.ps2 else "music/ffx_music.fev"
    e = ar.lookup(SOUND_ROOT + path)
    p = load(ar, e)
    grp = {g.path: g for g in p.all_groups()}.get("music")
    used = 0
    for t, ix in enumerate(table):
        if t == BATTLE_TRACK:
            ix = BATTLE_EVENT
        if ix < 0:
            continue
        used += 1
        ev = grp.events[ix] if grp and ix < len(grp.events) else None
        waves = ", ".join(describe_wave(p, w) for _, _, w in p.waves_of(ev)) if ev else ""
        print("track %3d -> music #%-3d %-36s %s" % (t, ix, ev.name if ev else "<missing>", waves))
    print("tracks with an event %d of %d, music events %d (%s)"
          % (used, BGM_TRACKS, len(grp.events) if grp else 0, path))


def cmd_voice(a, ar):
    ids = parse_ml(ar.read(ar.lookup(SOUND_ROOT + "voice/voiceidmapper.txt")))
    fevmap = parse_ml(ar.read(ar.lookup(SOUND_ROOT + "voice/%s/voicefevmapper.txt" % a.lang)))
    projects = {}
    for e in fev_entries(ar, "voice/%s/" % a.lang):
        p = load(ar, e)
        projects[p.name] = p
    by_slot = {}
    for name, p in projects.items():
        tail = name[-2:]
        if tail.isdigit():
            by_slot.setdefault(int(tail), name)
    btl = projects.get("ffx_%s_voice_btl" % a.lang)
    iop = ar.lookup(SOUND_ROOT + "voice/%s/ffx_%s_voice_btl_iop_bank00.fsb" % (a.lang, a.lang))
    import fsb
    shout_names = {s.name for s in fsb.Fsb5(ar.read(iop)).subsounds} if iop else set()

    def find(p, name):
        if p is None:
            return None
        for g in p.all_groups():
            if g.path == "voice":
                for ev in g.events:
                    if ev.name == name:
                        return ev
        return None

    c = Counter()
    special = {137630980, 206853380, 678712580, 678716676, 1284}
    for vid, name in ids:
        if vid > 0x33000000 or vid in special:
            where, ev = "voice_btl", find(btl, name)
        else:
            slot = voice_slot(name)
            pname = by_slot.get(slot)
            where, ev = (pname or "slot %d" % slot), find(projects.get(pname), name)
            if ev is None:
                # FmodVoice__prepare forces slot 0 while g_ffxFmodVoiceBusyDisabled is set
                # (maps 393-398) or on map 270, and dataChange loads those maps' projects
                # into slot 0, whatever the name says
                for forced in ("ffx_%s_voice_nagisetsu" % a.lang, "ffx_%s_voice270" % a.lang):
                    ev = find(projects.get(forced), name)
                    if ev is not None:
                        where = forced + " (slot 0)"
                        break
        if ev is None and name in shout_names:
            where = "shout fsb"
            c["shout subsound"] += 1
        elif ev is None:
            c["not found"] += 1
        else:
            c["event"] += 1
        print("0x%08X  %-20s %-24s %s" % (vid, name, where, "event" if ev else "-"))
    print("voice ids %d: %s" % (len(ids), dict(c)))
    print("VoiceFevMapper %s: %d maps -> %d projects" % (a.lang, len(fevmap),
                                                          len({n for _, n in fevmap})))


def main(argv=None):
    p = argparse.ArgumentParser(description="Read the FMOD Designer .fev projects of FFX HD Remaster.")
    p.add_argument("--vbf", default=vbf.DEFAULT_VBF)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name, fn in [("projects", cmd_projects), ("summary", cmd_summary), ("check", cmd_check),
                     ("seq", cmd_seq)]:
        sp = sub.add_parser(name)
        sp.add_argument("filter", nargs="?")
        sp.set_defaults(fn=fn)
    sp = sub.add_parser("events")
    sp.add_argument("filter", nargs="?")
    sp.add_argument("--names", action="store_true")
    sp.set_defaults(fn=cmd_events)
    sp = sub.add_parser("dump")
    sp.add_argument("project")
    sp.set_defaults(fn=cmd_dump)
    sp = sub.add_parser("bgm")
    sp.add_argument("--ps2", action="store_true")
    sp.add_argument("--exe", default=DEFAULT_EXE)
    sp.set_defaults(fn=cmd_bgm)
    sp = sub.add_parser("voice")
    sp.add_argument("--lang", choices=["us", "jp"], default="us")
    sp.set_defaults(fn=cmd_voice)
    a = p.parse_args(argv)
    with vbf.VbfArchive(a.vbf) as ar:
        try:
            return a.fn(a, ar) or 0
        except FevError as ex:
            print("error: %s" % ex, file=sys.stderr)
            return 2


if __name__ == "__main__":
    sys.exit(main())
