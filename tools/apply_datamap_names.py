"""
Apply the .data allocation map names from reversing/DATA_MAP.md to the FFX.exe IDB.

These are the big static buffers and arenas in FFX.exe's zero filled .data that tools/datamap.py
explains by hand (DATA_MAP.md section 3), plus the bump cursors and limits that hand them out. Every
row is a placeholder today (unk_, dword_, byte_) or has no name at all. Each comment gives the size
and the instruction that proves it.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all. A name starting maybe_ is a guess
and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_datamap_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
COMMENTS = False
VERBOSE = False

# ---------------------------------------------------------------------------------------------
# Functions and code labels whose current name is a placeholder
# ---------------------------------------------------------------------------------------------

NAMES = {
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    # ppp heaps and part slots
    0xCEC258: ("g_ffxMapFxPppHeapArena", "256 KB (0x40000) ppp heap for the map particle set. FFX_MapFx_LoadParticleSet pushes 40000h at 0x72A814 and this address at 0x72A819 for FFX_Ppp_InitHeapForSet. Zero fill, step thread. DATA_MAP.md 3"),
    0xD32860: ("g_ffxPppPartSlots", "256 ppp part slots of 396 bytes (0x18C00), the PS2 partmast part table. Cleared by memset(.., 0, 18C00h) at 0x72AC30, indexed 396 * slot by PppCore__f72C1B0. DATA_MAP.md 3"),
    0xFD24F0: ("g_ffxVfxPppHeapArena", "1 MB (0x100000) ppp heap for the PMCOM effect slots: FFX_Ppp_InitHeapForSet(dword_10D4CFC, 0, this, 100000h) at 0x760615 and 0x761DBA. Only the dev link fills it. DATA_MAP.md 3"),
    0x113FE10: ("g_ffxResidentEffectHeapArena", "384 KB (0x60000) heap of resident effect unit 0, set up once at boot by sub_7FD760 (push 60000h at 0x7FD78F, this at 0x7FD794). DATA_MAP.md 3"),
    0x2343520: ("g_ffxResidentPppHeapArena", "512 KB (0x80000) resident magic ppp heap. FFX_MagicResident_OvlInit pushes 80000h at 0x8005C6 and this at 0x8005CB. DATA_MAP.md 3"),
    0x16C1870: ("g_ffxSphereGridPppHeapArena", "sphere grid ppp heap inside the menu arena. FFX_SphereGrid_StepWarp pushes 7D000h at 0xA48079 but the next buffer is 0x6F000 on, see DATA_MAP.md 3. Step thread, menu only"),
    # effect packet arenas and their cursors
    0x11A1260: ("g_ffxEffectPacketArenaA", "per frame effect packet arena A, 0xF0000 used: sub_7EC890 stores it in g_ffxEffectPacketArenaABase (0x7EC8AB) and FFX_MagicResident_OvlEffectsPass adds 0F0000h for the limit (0x80069D). The sphere grid board effect file also loads at its start. DATA_MAP.md 3"),
    0x2332E90: ("g_ffxEffectPacketArenaABase", "base of effect packet arena A, = &g_ffxEffectPacketArenaA, set by sub_7EC890 at 0x7EC8AB. Read by FFX_MagicResident_OvlStep / OvlEffectsPass / HdrOef2Pass2 to reset the cursor. DATA_MAP.md 3"),
    0x2332E8C: ("g_ffxEffectPacketCursorA", "bump cursor into effect packet arena A. Reset to the base and advanced by FFX_MagicResident_OvlStep, OvlEffectsPass and HdrOef2Pass2. DATA_MAP.md 3"),
    0x2332E88: ("g_ffxEffectPacketLimitA", "end of effect packet arena A for the current pass, base + 0F0000h (0x80069D). Written by FFX_MagicResident_OvlStep, OvlEffectsPass, HdrOef2Pass2. DATA_MAP.md 3"),
    0x2332E84: ("g_ffxEffectPacketArenaBBase", "base of effect packet arena B. sub_7EC890 stores the literal 0x1D00000 here (0x7EC8B5), a PS2 main RAM address that lands in g_ffxPs2AddressWindow, not a relocated pointer. DATA_MAP.md 4"),
    0x2332E80: ("g_ffxEffectPacketCursorB", "bump cursor into effect packet arena B, advanced by opu_part_run (0x80BCB1, 0x80BCF2). Reset from g_ffxEffectPacketArenaBBase. DATA_MAP.md 4"),
    # PMCOM dev link and the dev printf ring
    0xD524A0: ("g_ffxPmcomRecvCursor", "bump cursor into g_ffxPmcomRecvArena. sub_760610 resets it (0x760623), sub_760080 advances it. Dev link only. DATA_MAP.md 3"),
    0xD524A4: ("g_ffxPmcomRingCursor", "write cursor into g_ffxPmcomRing. sub_760610 resets it (0x76062D), sub_762F50 wraps it at the ring end (0x762F6C). Dev link only. DATA_MAP.md 3"),
    0xD524A8: ("g_ffxPmcomRecvArena", "1 MB (0x100000) PMCOM receive arena, PMCOM being the PS2 particle master protocol of the dev tool link. Handed out by g_ffxPmcomRecvCursor. Never touched in a retail run. DATA_MAP.md 3"),
    0xE524F0: ("g_ffxPmcomRing", "1 MB (0x100000) PMCOM ring, wrapped at its end by sub_762F50 (0x762F6C). Its first 0xD0000 also takes the 512x416 screen grab (sub_761E20) and 0xF224F0.. the RGB copy. Dev link only. DATA_MAP.md 3"),
    0xF524F0: ("g_ffxPmcomDoubleBuffer", "512 KB PMCOM double buffer, two halves of 0x40000 picked by dword_D4B4C0 & 1 (sub_760080 0x7601CB). Dev link only. DATA_MAP.md 3"),
    0x10E7878: ("g_ffxDevPrintfRingIndex", "line index into g_ffxDevPrintfRing, wraps at 256. sub_76B860 advances it, sub_76B8D0 drains up to it. DATA_MAP.md 3"),
    0x10E7880: ("g_ffxDevPrintfRing", "256 vsprintf lines of 1028 bytes (0x40400) for the dev link, sub_76B860 indexes imul 404h at 0x76B88D. DATA_MAP.md 3"),
    # inside the 5 MB menu arena 0x1340870..0x1840870 (FFX_MenuSys_RunFrame push 500000h at 0x8AB0D2)
    0x15C0870: ("g_ffxMenuGsPacketBuffer", "512 KB (0x80000) menu GS packet buffer inside the menu arena. sub_8BEF30 pushes 80000h at 0x8BEF35 and sub_9047C0(this, 80000h, 480000). DATA_MAP.md 3"),
    0x16AB1A0: ("g_ffxMenuPageRecords", "menu page records, 0x2710 bytes inside the menu arena. sub_A572E0 clears 9C4h dwords at 0xA572F0 then stores it in g_ffxMenuPages. DATA_MAP.md 3"),
    0x16AD8B0: ("g_ffxMenuWorkBlock", "menu work block, 0x12FC0 bytes inside the menu arena. sub_A572E0 clears 4BF0h dwords at 0xA57333 then stores it in g_ffxMenuWork. DATA_MAP.md 3"),
    0x1760870: ("g_ffxMenuEffectPacketBuffer", "384 KB menu effect packet buffer inside the menu arena, sized to the next reference [I]. sub_A545A0 and FFX_Menu_DrawAllPartsIgnoringCatchUp point g_ffxVfxPacketCursor at it. DATA_MAP.md 3"),
    # the rest
    0x1A8610C: ("g_ffxPs2AddressWindow", "8 MB (0x800034) with no relocation in it at all, the PE .reloc and the IDB fixups agree. Code writes PS2 main RAM addresses into it as literals (0x1C80000 textures, 0x1D00000 effect arena B, 0x1F00000 menu GS packets). Port it as a real 8 MB block. DATA_MAP.md 4"),
    0x2333500: ("g_ffxOef2Op078FallbackBuf", "64 KB fallback buffer FFX_Oef2_Op078 uses when sub_7B7EB0 returns null (0x80F2AE). Size from the next reference [I]. DATA_MAP.md 3"),
}

PLACEHOLDER = re.compile(
    r"^(sub|nullsub|loc|j_sub|unk|dword|word|byte|qword|off|flt|dbl|stru|funcs)_[0-9A-Fa-f]+$"
    r"|^[A-Za-z]+__f[0-9A-Fa-f]{6}$"
    r"|^FFX_AtelSys_[A-Za-z]+_\d+_(start|poll|resf|resi)$")


def _cur_name(ea):
    import idc
    return idc.get_name(ea) or ""


def _plan(table, kind):
    import idaapi
    import ida_name
    plan, skips, kept = [], [], []
    for ea, (new, why) in sorted(table.items()):
        cur = _cur_name(ea)
        if cur == new:
            kept.append("0x%X %s" % (ea, new))
            continue
        m = re.match(r"^was ([^ ]+)\.", why)
        if kind == "RENAMES" or (kind == "GLOBALS" and m):
            if not m:
                skips.append("0x%X %s: comment does not start 'was OLD.'" % (ea, new))
                continue
            if cur != m.group(1):
                skips.append("0x%X is %s, expected %s" % (ea, cur or "(no name)", m.group(1)))
                continue
        else:
            if cur and not PLACEHOLDER.match(cur):
                skips.append("0x%X is %s, not a placeholder" % (ea, cur))
                continue
        other = ida_name.get_name_ea(idaapi.BADADDR, new)
        if other != idaapi.BADADDR and other != ea:
            skips.append("0x%X name %s already used at 0x%X" % (ea, new, other))
            continue
        plan.append((ea, cur, new, why))
    return plan, skips, kept


def _add_comment(ea, text):
    import idc
    import ida_funcs
    if not text:
        return False
    f = ida_funcs.get_func(ea)
    if f and f.start_ea == ea:
        old = idc.get_func_cmt(ea, 1) or ""
        if text in old or len(old) + len(text) + 1 > 1000:
            return False
        return bool(idc.set_func_cmt(ea, (old + "\n" + text) if old else text, 1))
    old = idc.get_cmt(ea, 1) or ""
    if text in old or len(old) + len(text) + 1 > 1000:
        return False
    return bool(idc.set_cmt(ea, (old + " " + text) if old else text, 1))


def main(dry_run=DRY_RUN, comments=COMMENTS, verbose=VERBOSE):
    import idc
    import ida_name
    total = 0
    failed = 0
    for kind, table in (("NAMES", NAMES), ("RENAMES", RENAMES), ("GLOBALS", GLOBALS)):
        plan, skips, kept = _plan(table, kind)
        for line in kept:
            if verbose:
                print("ok    %s %s" % (kind, line))
        for line in skips:
            print("skip  %s %s" % (kind, line))
        for ea, cur, new, why in plan:
            print("name  %s 0x%X %s -> %s%s" % (kind, ea, cur or "(no name)", new, ("    " + why) if verbose else ""))
        print("%s: %d to rename, %d skipped, %d already named, %d rows" % (kind, len(plan), len(skips), len(kept), len(table)))
        total += len(plan)
        if dry_run:
            continue
        for ea, cur, new, why in plan:
            if not idc.set_name(ea, new, ida_name.SN_NOWARN | ida_name.SN_NOCHECK):
                print("FAILED 0x%X %s -> %s" % (ea, cur, new))
                failed += 1
            elif comments:
                _add_comment(ea, why)
    if dry_run:
        print("DRY_RUN, nothing renamed (%d would be)" % total)
        return True
    print("renamed %d, failed %d. The database is NOT saved, save it when you are happy" % (total - failed, failed))
    return failed == 0


def headless():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True, help="a COPY of the database")
    ap.add_argument("--apply", action="store_true", help="rehearse the renames, the database is not saved")
    ap.add_argument("--comments", action="store_true", help="also append each row's comment")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    import idapro
    if idapro.open_database(a.idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % a.idb)
    try:
        main(dry_run=not a.apply, comments=a.comments, verbose=a.verbose)
    finally:
        idapro.close_database(save=False)


if __name__ == "__main__":
    if "idaapi" in sys.modules or "ida_kernwin" in sys.modules:
        main()
    else:
        headless()
