"""Read a Windows minidump without a debugger, for 32-bit FFX.exe crashes.

Windows has no debugger installed on this machine, and WER drops 50 MB dumps into
%LOCALAPPDATA%\\CrashDumps every time the game dies. This pulls out the part that
matters: the exception, the faulting address as module+RVA, the registers, and a
call chain, with every address also given as the VA the module's IDB uses.

    python tools/minidump.py <dump.dmp>
    python tools/minidump.py <dump.dmp> --thread 1234
    python tools/minidump.py <dump.dmp> --all-threads

The IDA column is RVA plus 0x400000 for FFX.exe, which is its preferred base. For
other modules the preferred base is not in the dump, so only module+RVA is shown.
"""

import struct
import sys
import os

# Stream types we care about.
THREAD_LIST = 3
MODULE_LIST = 4
MEMORY_LIST = 5
EXCEPTION = 6
SYSTEM_INFO = 7
MEMORY64_LIST = 9

FFX_PREFERRED_BASE = 0x400000

EXC_NAMES = {
    0xC0000005: "ACCESS_VIOLATION",
    0xC000001D: "ILLEGAL_INSTRUCTION",
    0xC0000025: "NONCONTINUABLE_EXCEPTION",
    0xC0000026: "INVALID_DISPOSITION",
    0xC000008C: "ARRAY_BOUNDS_EXCEEDED",
    0xC000008D: "FLT_DENORMAL_OPERAND",
    0xC000008E: "FLT_DIVIDE_BY_ZERO",
    0xC0000090: "FLT_INVALID_OPERATION",
    0xC0000091: "FLT_OVERFLOW",
    0xC0000093: "FLT_UNDERFLOW",
    0xC0000094: "INT_DIVIDE_BY_ZERO",
    0xC0000095: "INT_OVERFLOW",
    0xC0000096: "PRIV_INSTRUCTION",
    0xC00000FD: "STACK_OVERFLOW",
    0xC0000006: "IN_PAGE_ERROR",
    0x80000003: "BREAKPOINT",
    0x80000004: "SINGLE_STEP",
    0xE06D7363: "a C++ exception nobody caught",
    0x406D1388: "the thread-name notification, benign",
}


class Minidump(object):
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()

        sig, ver, nstreams, dir_rva, _csum, _ts, _flags = struct.unpack_from(
            "<IIIIIIQ", self.data, 0)
        if sig != 0x504D444D:  # "MDMP"
            raise ValueError("not a minidump, signature 0x%08X" % sig)

        self.streams = {}
        for i in range(nstreams):
            stype, size, rva = struct.unpack_from("<III", self.data, dir_rva + i * 12)
            # A stream type can repeat; keep the first, which is what debuggers use.
            if stype not in self.streams:
                self.streams[stype] = (size, rva)

        self.arch = None
        if SYSTEM_INFO in self.streams:
            _size, rva = self.streams[SYSTEM_INFO]
            self.arch = struct.unpack_from("<H", self.data, rva)[0]

        self.modules = self._read_modules()
        self.ranges = self._read_memory_ranges()
        self.threads = self._read_threads()

    # -- strings ------------------------------------------------------------
    def _string(self, rva):
        if rva == 0 or rva + 4 > len(self.data):
            return ""
        n = struct.unpack_from("<I", self.data, rva)[0]
        raw = self.data[rva + 4: rva + 4 + n]
        try:
            return raw.decode("utf-16-le", "replace")
        except Exception:
            return ""

    # -- modules ------------------------------------------------------------
    def _read_modules(self):
        out = []
        if MODULE_LIST not in self.streams:
            return out
        _size, rva = self.streams[MODULE_LIST]
        count = struct.unpack_from("<I", self.data, rva)[0]
        at = rva + 4
        for i in range(count):
            base, size, _csum, _ts, name_rva = struct.unpack_from("<QIIII", self.data, at)
            full = self._string(name_rva)
            out.append({
                "base": base,
                "size": size,
                "path": full,
                "name": os.path.basename(full.replace("\\", "/")),
            })
            at += 108
        return out

    def module_of(self, addr):
        for m in self.modules:
            if m["base"] <= addr < m["base"] + m["size"]:
                return m
        return None

    # -- memory -------------------------------------------------------------
    def _read_memory_ranges(self):
        """List of (start, size, file_offset), sorted by start."""
        out = []
        if MEMORY64_LIST in self.streams:
            _size, rva = self.streams[MEMORY64_LIST]
            count, base_rva = struct.unpack_from("<QQ", self.data, rva)
            at = rva + 16
            off = base_rva
            for i in range(count):
                start, dsize = struct.unpack_from("<QQ", self.data, at)
                out.append((start, dsize, off))
                off += dsize
                at += 16
        if MEMORY_LIST in self.streams:
            _size, rva = self.streams[MEMORY_LIST]
            count = struct.unpack_from("<I", self.data, rva)[0]
            at = rva + 4
            for i in range(count):
                start, dsize, drva = struct.unpack_from("<QII", self.data, at)
                out.append((start, dsize, drva))
                at += 16
        out.sort(key=lambda r: r[0])
        return out

    def read(self, addr, n):
        """Bytes at a virtual address, or None when the dump does not hold them."""
        for start, size, off in self.ranges:
            if start <= addr and addr + n <= start + size:
                fo = off + (addr - start)
                if fo + n <= len(self.data):
                    return self.data[fo: fo + n]
                return None
        return None

    def dword(self, addr):
        b = self.read(addr, 4)
        if b is None:
            return None
        return struct.unpack("<I", b)[0]

    # -- threads ------------------------------------------------------------
    def _read_threads(self):
        out = []
        if THREAD_LIST not in self.streams:
            return out
        _size, rva = self.streams[THREAD_LIST]
        count = struct.unpack_from("<I", self.data, rva)[0]
        at = rva + 4
        for i in range(count):
            (tid, _susp, _pc, _prio, teb, stack_start, stack_size, stack_rva,
             ctx_size, ctx_rva) = struct.unpack_from("<IIIIQQIIII", self.data, at)
            out.append({
                "tid": tid,
                "teb": teb,
                "stack_start": stack_start,
                "stack_size": stack_size,
                "ctx_size": ctx_size,
                "ctx_rva": ctx_rva,
            })
            at += 48
        return out

    def thread(self, tid):
        for t in self.threads:
            if t["tid"] == tid:
                return t
        return None

    # -- x86 CONTEXT --------------------------------------------------------
    def context_x86(self, ctx_rva, ctx_size):
        """The x86 CONTEXT fields we need. Offsets are the documented layout:
        ContextFlags 0x00, debug regs to 0x1C, FLOATING_SAVE_AREA 112 bytes to
        0x8C, segments, then Edi 0x9C .. Eax 0xB0, Ebp 0xB4, Eip 0xB8,
        SegCs 0xBC, EFlags 0xC0, Esp 0xC4, SegSs 0xC8."""
        if ctx_size < 0xCC:
            return None
        b = self.data[ctx_rva: ctx_rva + ctx_size]
        f = lambda o: struct.unpack_from("<I", b, o)[0]
        return {
            "ContextFlags": f(0x00),
            "Edi": f(0x9C), "Esi": f(0xA0), "Ebx": f(0xA4),
            "Edx": f(0xA8), "Ecx": f(0xAC), "Eax": f(0xB0),
            "Ebp": f(0xB4), "Eip": f(0xB8),
            "EFlags": f(0xC0), "Esp": f(0xC4),
        }

    # -- exception ----------------------------------------------------------
    def exception(self):
        if EXCEPTION not in self.streams:
            return None
        _size, rva = self.streams[EXCEPTION]
        tid, _align = struct.unpack_from("<II", self.data, rva)
        at = rva + 8
        code, flags, _rec, addr, nparams, _un = struct.unpack_from("<IIQQII", self.data, at)
        params = []
        for i in range(min(nparams, 15)):
            params.append(struct.unpack_from("<Q", self.data, at + 40 + i * 8)[0])
        ctx_size, ctx_rva = struct.unpack_from("<II", self.data, at + 152)
        return {
            "tid": tid, "code": code, "flags": flags, "address": addr,
            "params": params, "ctx_size": ctx_size, "ctx_rva": ctx_rva,
        }


def fmt_addr(dump, addr):
    if addr is None:
        return "?"
    m = dump.module_of(addr)
    if not m:
        return "0x%08X  (not in any module)" % addr
    rva = addr - m["base"]
    s = "0x%08X  %s+0x%08X" % (addr, m["name"], rva)
    if m["name"].lower() == "ffx.exe":
        s += "  ida 0x%08X" % (rva + FFX_PREFERRED_BASE)
    return s


def looks_like_call_site(dump, ret):
    """Is the code just before ret a call? Returns True, False, or None when the
    dump does not contain those bytes."""
    b = dump.read(ret - 7, 7)
    if b is None:
        return None
    if b[2] == 0xE8:          # call rel32, the 5 bytes ending at ret
        return True
    if b[0] == 0x9A:          # far call
        return True
    for back in range(2, 8):  # call r/m32 is FF /2, 2 to 7 bytes long
        o = 7 - back
        if o + 1 < len(b) and b[o] == 0xFF and ((b[o + 1] >> 3) & 7) == 2:
            return True
    return False


def walk_frames(dump, ebp, lo, hi, limit=64):
    out = []
    cur = ebp
    for _ in range(256):
        if len(out) >= limit:
            break
        if cur is None or cur < lo or cur + 8 > hi or (cur & 3):
            break
        ret = dump.dword(cur + 4)
        nxt = dump.dword(cur)
        if ret is None or nxt is None:
            break
        if dump.module_of(ret):
            out.append(ret)
        if nxt <= cur:
            break
        cur = nxt
    return out


def scan_stack(dump, esp, lo, hi, limit=60, span=0x8000):
    out = []
    at = esp & ~3
    if at < lo:
        at = lo
    stop = min(hi, at + span)
    while at + 4 <= stop and len(out) < limit:
        v = dump.dword(at)
        if v is not None and dump.module_of(v):
            c = looks_like_call_site(dump, v)
            if c is not False:
                out.append((at, v, c))
        at += 4
    return out


def report_thread(dump, t, ctx, label):
    print("")
    print("=" * 78)
    print("%s  thread %d (0x%X)" % (label, t["tid"], t["tid"]))
    print("=" * 78)
    if not ctx:
        print("  no usable x86 context")
        return

    print("  eip = %s" % fmt_addr(dump, ctx["Eip"]))
    print("  eax=%08X ebx=%08X ecx=%08X edx=%08X" % (ctx["Eax"], ctx["Ebx"], ctx["Ecx"], ctx["Edx"]))
    print("  esi=%08X edi=%08X ebp=%08X esp=%08X flg=%08X"
          % (ctx["Esi"], ctx["Edi"], ctx["Ebp"], ctx["Esp"], ctx["EFlags"]))

    code = dump.read(ctx["Eip"], 16)
    if code:
        print("  bytes at eip: %s" % " ".join("%02X" % c for c in bytearray(code)))

    lo = t["stack_start"]
    hi = t["stack_start"] + t["stack_size"]
    print("  stack in dump: 0x%08X..0x%08X" % (lo, hi))

    print("")
    print("  frame pointer walk:")
    frames = [ctx["Eip"]] + walk_frames(dump, ctx["Ebp"], lo, hi)
    for i, a in enumerate(frames):
        print("   %3d  %s" % (i, fmt_addr(dump, a)))

    print("")
    print("  stack scan, nearest first. HAS STALE ENTRIES, read as candidates:")
    for at, v, c in scan_stack(dump, ctx["Esp"], lo, hi):
        mark = "" if c else "   (call site unverified, code not in dump)"
        print("   esp+%04X  %s%s" % (at - ctx["Esp"], fmt_addr(dump, v), mark))


# ---------------------------------------------------------------------------
# Finding the ORIGINAL exception when a crash reporter faulted on top of it
# ---------------------------------------------------------------------------
#
# FFX ships Square Enix's CSERHelper.dll, which handles the exception itself and
# calls the game's own minidump writer. When that machinery faults, the dump WER
# then writes describes the reporter's fault and not the real one. The real
# EXCEPTION_RECORD and CONTEXT are still sitting on the faulting thread's stack,
# because the same thread ran the handler, so scan for them.
#
# x86 EXCEPTION_RECORD: code +0x00, flags +0x04, inner record +0x08,
# address +0x0C, parameter count +0x10, parameters +0x14. Size 0x50.

CONTEXT_FLAG_CANDIDATES = (0x1003F, 0x1003B, 0x10007, 0x1001F, 0x10017, 0x10001,
                           0x1000F, 0x10010, 0x10002, 0x10003, 0x1002F, 0x10027)


def find_exception_records(dump, lo, hi):
    out = []
    at = lo & ~3
    while at + 0x50 <= hi:
        code = dump.dword(at)
        if code in EXC_NAMES and code not in (0x406D1388,):
            flags = dump.dword(at + 4)
            addr = dump.dword(at + 0x0C)
            nparams = dump.dword(at + 0x10)
            if (flags is not None and flags <= 0xFF and nparams is not None
                    and nparams <= 15 and addr and dump.module_of(addr)):
                params = [dump.dword(at + 0x14 + i * 4) for i in range(min(nparams, 4))]
                out.append((at, code, flags, addr, nparams, params))
        at += 4
    return out


def find_contexts(dump, lo, hi):
    out = []
    at = lo & ~3
    while at + 0x2CC <= hi:
        flags = dump.dword(at)
        if flags in CONTEXT_FLAG_CANDIDATES:
            eip = dump.dword(at + 0xB8)
            esp = dump.dword(at + 0xC4)
            ebp = dump.dword(at + 0xB4)
            if eip and dump.module_of(eip) and esp and lo <= esp <= hi:
                out.append((at, eip, esp, ebp))
        at += 4
    return out


def report_original(dump, tid):
    t = dump.thread(tid)
    if not t:
        print("no thread %d" % tid)
        return
    lo = t["stack_start"]
    hi = lo + t["stack_size"]
    print("")
    print("=" * 78)
    print("SCANNING thread %d stack 0x%08X..0x%08X for the original exception"
          % (tid, lo, hi))
    print("=" * 78)

    recs = find_exception_records(dump, lo, hi)
    if not recs:
        print("  no EXCEPTION_RECORD found on this stack")
    for at, code, flags, addr, nparams, params in recs:
        print("")
        print("  EXCEPTION_RECORD at 0x%08X" % at)
        print("    code    : 0x%08X %s" % (code, EXC_NAMES.get(code, "unknown")))
        print("    flags   : 0x%X%s" % (flags, "  (noncontinuable)" if flags & 1 else ""))
        print("    address : %s" % fmt_addr(dump, addr))
        if code == 0xC0000005 and nparams >= 2:
            kind = {0: "reading", 1: "writing", 8: "executing"}.get(params[0], "?")
            print("    detail  : %s 0x%08X" % (kind, params[1]))
        code_bytes = dump.read(addr, 16)
        if code_bytes:
            print("    bytes   : %s" % " ".join("%02X" % c for c in bytearray(code_bytes)))

    ctxs = find_contexts(dump, lo, hi)
    print("")
    print("  CONTEXT records found: %d" % len(ctxs))
    for at, eip, esp, ebp in ctxs[:6]:
        print("    at 0x%08X  eip = %s" % (at, fmt_addr(dump, eip)))
        print("                  esp=0x%08X ebp=0x%08X" % (esp, ebp))
        frames = [eip] + walk_frames(dump, ebp, lo, hi, limit=24)
        print("      frame walk from that context:")
        for i, a in enumerate(frames):
            print("       %3d  %s" % (i, fmt_addr(dump, a)))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1

    path = argv[1]
    want_all = "--all-threads" in argv
    want_tid = None
    if "--thread" in argv:
        want_tid = int(argv[argv.index("--thread") + 1], 0)

    dump = Minidump(path)

    print("dump    : %s (%.1f MB)" % (os.path.basename(path), len(dump.data) / 1048576.0))
    print("arch    : %s" % {0: "x86", 9: "x64", 5: "ARM"}.get(dump.arch, "unknown %s" % dump.arch))
    print("modules : %d, memory ranges: %d, threads: %d"
          % (len(dump.modules), len(dump.ranges), len(dump.threads)))

    ffx = None
    for m in dump.modules:
        if m["name"].lower() == "ffx.exe":
            ffx = m
    if ffx:
        print("FFX.exe : base 0x%08X size 0x%08X, so ida = address - 0x%08X + 0x%08X"
              % (ffx["base"], ffx["size"], ffx["base"], FFX_PREFERRED_BASE))

    print("")
    print("Al Bhed modules present:")
    any_ours = False
    for m in dump.modules:
        if "albhedworkshop" in m["path"].lower() or m["name"].lower() in (
                "dinput8.dll", "albhedcheats.dll", "pilgrimagetogether.dll"):
            print("  0x%08X size 0x%08X  %s" % (m["base"], m["size"], m["path"]))
            any_ours = True
    if not any_ours:
        print("  none")

    exc = dump.exception()
    if exc:
        print("")
        print("EXCEPTION")
        print("  code    : 0x%08X %s" % (exc["code"], EXC_NAMES.get(exc["code"], "unknown")))
        print("  address : %s" % fmt_addr(dump, exc["address"]))
        if exc["code"] == 0xC0000005 and len(exc["params"]) >= 2:
            kind = {0: "reading", 1: "writing", 8: "executing"}.get(exc["params"][0], "?")
            print("  detail  : %s 0x%08X" % (kind, exc["params"][1]))
        print("  thread  : %d" % exc["tid"])

        t = dump.thread(exc["tid"])
        ctx = dump.context_x86(exc["ctx_rva"], exc["ctx_size"])
        if t:
            report_thread(dump, t, ctx, "FAULTING THREAD")

    if want_tid is not None:
        t = dump.thread(want_tid)
        if not t:
            print("no thread %d in the dump" % want_tid)
        else:
            report_thread(dump, t, dump.context_x86(t["ctx_rva"], t["ctx_size"]), "THREAD")

    if "--find-original" in argv:
        tid = exc["tid"] if exc else (dump.threads[0]["tid"] if dump.threads else None)
        if "--thread" in argv:
            tid = int(argv[argv.index("--thread") + 1], 0)
        if tid is not None:
            report_original(dump, tid)

    if want_all:
        for t in dump.threads:
            if exc and t["tid"] == exc["tid"]:
                continue
            ctx = dump.context_x86(t["ctx_rva"], t["ctx_size"])
            if not ctx:
                continue
            # Only threads with at least one of our modules or FFX.exe on the stack,
            # otherwise this is pages of system worker threads.
            lo = t["stack_start"]
            hi = lo + t["stack_size"]
            frames = [ctx["Eip"]] + walk_frames(dump, ctx["Ebp"], lo, hi, limit=24)
            interesting = any(
                (dump.module_of(a) or {}).get("name", "").lower()
                in ("ffx.exe", "albhedcheats.dll", "pilgrimagetogether.dll", "dinput8.dll")
                for a in frames)
            if interesting:
                report_thread(dump, t, ctx, "THREAD")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
