"""
Apply the rcData names from reversing/RCDATA.md to the FFX.exe IDB.

rcData is the PS2 data library, four object files from ..\\program\\src\\rcData\\:

    rcdtTm2.c    0x923240..0x923B5E  TIM2 texture loader
    rcdtRsd2.c   0x923B60..0x92476E  the PS1 style RSD model set (.rsd, .ply, .mat, .ma2)
    rcdtChunk.c  0x92B7D0..0x92B954  a {data, size} buffer, optionally loaded from a file
    rcdtParse.c  0x92B960..0x92C125  a cursor over a chunk, text tokens or raw binary

The exact names rcdtRsdLoad (its own "rcdtRsdLoad : File not found." message) and the three
rcdtRsdLoad__* statics survive. The rest follow the alphabetical layout law (LUA_MODULE.md) where
the file is small enough to check it, and the decompiled behaviour everywhere. Only the chunk
buffer and the binary half of the cursor are live in retail, used by the rcBg map stream loader.
The text parsers and the TIM2 loader are reached only from orphan debug roots.

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

Several rows name code that IDA has not made a function (orphans with no reference at all). The
name lands on the code label and the comment becomes an address comment. Creating the functions is
left to whoever applies this.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_rcdata_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # rcdtTm2.c, the TIM2 loader. Object 0x50 bytes: +0 file header, +16 first picture header,
    # +64 pixels, +68 clut (both PrxPtr handles), +72 GS PSM. RCDATA.md 4.1
    0x923240: ("rcdtTm2_ConvertClutAlpha", "static. Walks 256 clut entries: alpha a != 0 becomes a*128/255 (the PS2 0..0x80 range), alpha 0 zeroes the whole entry. Always 256, so a 16 colour clut is overrun. DEAD in retail, RCDATA.md 4.1"),
    0x923280: ("rcdtTm2_GetPsm", "static. GS PSM for the picture header's image type (+19) from g_rcdtTm2PsmByImageType: 1->2 PSMCT16, 2->1 PSMCT24, 3->0 PSMCT32, 4->0x14 PSMT4, 5->0x13 PSMT8. 0 for type 0"),
    0x923450: ("rcdtTm2Delete", "Final then free"),
    0x923470: ("rcdtTm2Final", "frees the pixel (+64) and clut (+68) buffers and clears both handles"),
    0x9234D0: ("rcdtTm2GetAlphaRect", "(obj, &x, &y, &w, &h) bounding rect of 8bpp pixels whose clut entry has nonzero alpha, x rounded down and w up to even. Caller is rcbg text loader code only"),
    0x923660: ("rcdtTm2GetBlackRect", "(obj, &x, &y, &w, &h) bounding rect of 8bpp pixels whose clut colour has rgb == 0. Used on the rcbg '%spos.tm2' position images"),
    0x9237E0: ("rcdtTm2Init", "memset 0x50"),
    0x923800: ("rcdtTm2Load", "(obj, path) = rcdtTm2_LoadFile(obj, path, 1), with the clut alpha conversion. Callers are rcbgLoadText code only, all under orphan roots. DEAD in retail, RCDATA.md 5"),
    0x923820: ("rcdtTm2UploadImage", "orphan, no reference of any kind. Pushes the pixels and then the 256 or 16 colour clut (+30 colours) through maybe_FFX_Gs_LoadImage425_Stub and _423_Stub, which return 0 on PC"),
    0x923960: ("rcdtTm2LoadNoAlpha", "orphan, no reference of any kind. rcdtTm2_LoadFile(obj, path, 0), the load without the clut alpha conversion"),
    0x923980: ("rcdtTm2ReturnInvalid", "orphan, no reference of any kind. or eax, -1 then ret"),
    0x923990: ("rcdtTm2UploadPixels", "orphan, no reference of any kind. Pushes the pixels alone through the GS LoadImage stubs with the PSM at +72"),
    0x923A10: ("rcdtTm2UploadRect", "orphan, no reference of any kind. Copies a sub rect out with rcdtTm2CopyRect into a temporary buffer, rows padded to 8 bytes, height to even, then pushes it through the GS LoadImage stubs"),
    0x923AE0: ("rcdtTm2CopyRect", "(obj, dst, x, y, w, rows) copies w bytes per row of 8bpp pixels starting at (x, y), dst row stride (w+7)&~7"),
    # rcdtRsd2.c, the RSD model set. Object 32 bytes: +0 nVert, +4 vert vec4[], +8 nNorm, +12 norm
    # vec4[], +16 nPoly, +20 poly[88], +24 nTex (never written), +28 tex name[]. RCDATA.md 4.2
    0x923B60: ("rcdtRsdLoad__LoadMat", "static (rsd, path). Dispatches on the extension: 'mat' -> rcdtRsdLoad__LoadMatRsd, 'ma2' -> rcdtRsdLoad__LoadMatRsd2, anything else is ignored"),
    0x9242D0: ("rcdtRsdDelete", "Final then free"),
    0x9242F0: ("rcdtRsdFinal", "frees the vertex (+4), normal (+12) and polygon (+20) arrays and the +24 texture names of +28. +24 is never written by rcdtRsdLoad, so the names leak"),
    0x924390: ("rcdtRsdInit", "zeroes the 32 byte RSD object"),
    # rcdtChunk.c, a {data, size} buffer. RCDATA.md 4.3
    0x92B7D0: ("rcdtChunkDelete", "Final then free. LIVE, the rcBg map stream frees its chunks with it"),
    0x92B7F0: ("rcdtChunkFinal", "frees the data pointer (+0) if set and clears it"),
    0x92B820: ("rcdtChunkGetEnd", "data + size"),
    0x92B830: ("rcdtChunkGetSize", "returns +4"),
    0x92B840: ("rcdtChunkInit", "data = 0, size = 0"),
    0x92B860: ("rcdtChunkIsEmpty", "data == 0 or size == 0"),
    0x92B880: ("rcdtChunkLoad", "(chunk, path) Final, then FFX_File_OpenHost0(path, 1), seek to the end for the size, read the whole file into a heap buffer and rcdtChunkSet it. Returns 1, or 0 when the open fails. DEAD in retail, every caller is under an orphan root"),
    0x92B900: ("rcdtChunkNew", "allocates and inits an 8 byte chunk"),
    0x92B920: ("rcdtChunkSet", "(chunk, data, size)"),
    0x92B940: ("rcdtChunkSetMemory", "(chunk, data) size = 0x7FFFFFFF, an unbounded view over memory. LIVE, rcbgMain's section walker wraps each in memory section with it"),
    # rcdtParse.c, the cursor. Object 0x8C bytes: +0 chunk, +4 cursor, +8 text mode, +12 a 128 byte
    # delimiter table indexed by the unsigned byte. RCDATA.md 4.4
    0x92B960: ("rcdtParseSkip", "cursor += n. LIVE"),
    0x92B970: ("rcdtParseSkipLine", "cursor = rcdtParseSkipTo(newline) + 1, no delimiter skip"),
    0x92B990: ("rcdtParseSkipTo", "cursor = rcdtParseFindChar(c)"),
    0x92B9B0: ("rcdtParseNextLine", "SkipLine then SkipDelimiters. How the text loaders step over '@' and '#' lines"),
    0x92B9D0: ("rcdtParseSkipBlock", "orphan, no reference of any kind. At '(' '{' or '[' skips to the matching close with nesting, then the delimiters"),
    0x92BA30: ("rcdtParseSkipToken", "cursor = TokenEnd, then SkipDelimiters"),
    0x92BA60: ("rcdtParseFinal", "empty. rcbgMain calls it after every section, LIVE"),
    0x92BA70: ("rcdtParseGetBase", "the chunk's data pointer"),
    0x92BA90: ("rcdtParseRead", "(parse, dst, n) memcpy n bytes from the cursor, cursor += n. LIVE, the rcBg map stream reads every field with it"),
    0x92BAC0: ("rcdtParseReadChar", "orphan, no reference of any kind. Returns the byte at the cursor, cursor += 1"),
    0x92BAE0: ("rcdtParseFindChar", "returns a pointer to the next c at or after the cursor, or the chunk end"),
    0x92BB30: ("rcdtParseReadInt32", "orphan, no reference of any kind. Raw little endian dword, cursor += 4"),
    0x92BB50: ("rcdtParseReadInt32Swap", "orphan, no reference of any kind. Dword with the bytes reversed, cursor += 4"),
    0x92BB90: ("rcdtParseGetEnd", "rcdtChunkGetEnd of the cursor's chunk"),
    0x92BBB0: ("rcdtParseGetFloat", "text mode: atof at the cursor then SkipToken. Binary mode: raw float, cursor += 4"),
    0x92BC00: ("rcdtParseReadFloatSwap", "orphan, no reference of any kind. Float with the bytes reversed, cursor += 4"),
    0x92BC50: ("rcdtParseGetInt", "text mode: atoi at the cursor then SkipToken. Binary mode: raw int, cursor += 4"),
    0x92BC90: ("rcdtParseGetLong", "orphan, no reference of any kind. Like rcdtParseGetInt with atol"),
    0x92BCD0: ("rcdtParseReadUInt32Swap", "orphan, no reference of any kind. Same bytes as rcdtParseReadInt32Swap"),
    0x92BD10: ("rcdtParseGetMode", "orphan, no reference of any kind. Returns +8, the text mode flag"),
    0x92BD20: ("rcdtParseTokenEnd", "pointer to the first delimiter at or after the cursor, or the chunk end"),
    0x92BD60: ("rcdtParseSkipDelimiters", "pointer past the run of delimiters at the cursor"),
    0x92BDA0: ("rcdtParseGetCursor", "orphan, no reference of any kind. Returns +4"),
    0x92BDB0: ("rcdtParseGetOffset", "orphan, no reference of any kind. cursor - chunk data"),
    0x92BDD0: ("rcdtParseGetString", "(parse, dst) copies the token, NUL terminates, SkipToken. Returns the length, or 0 outside text mode"),
    0x92BE10: ("rcdtParseGetTokenLength", "token length in text mode, 0 in binary mode"),
    0x92BE40: ("rcdtParseGetKeyword", "(parse, table) index of the keyword whose length equals the token's and strnicmp matches, else -1. Then SkipToken"),
    0x92BEE0: ("rcdtParseReadInt16", "orphan, no reference of any kind. Raw word, cursor += 2"),
    0x92BF00: ("rcdtParseReadInt16Swap", "orphan, no reference of any kind. Word with the bytes swapped, cursor += 2"),
    0x92BF30: ("rcdtParseTokenLength", "TokenEnd - cursor"),
    0x92BF50: ("rcdtParseInit", "chunk, cursor and mode 0 (binary), then ResetDelimiters. LIVE"),
    0x92BF80: ("rcdtParseIsEmpty", "orphan, no reference of any kind. 1 when there is no chunk or rcdtChunkIsEmpty"),
    0x92BFB0: ("rcdtParseIsEnd", "cursor >= chunk end"),
    0x92BFD0: ("rcdtParseNew", "allocates and inits a 0x8C byte cursor"),
    0x92C000: ("rcdtParseResetDelimiters", "clears the 128 byte table at +12 and marks bytes 0x00..0x20 as delimiters. LIVE through Init, though binary mode never consults it"),
    0x92C060: ("rcdtParsePeekFloat", "orphan, no reference of any kind. atof at the cursor, no advance"),
    0x92C080: ("rcdtParsePeekInt", "orphan, no reference of any kind. atoi at the cursor, no advance"),
    0x92C0A0: ("rcdtParseSetChunk", "(parse, chunk) cursor = chunk data. LIVE"),
    0x92C0C0: ("rcdtParseSetText", "(parse, on) sets +8. Only the dead text loaders pass 1"),
    0x92C0D0: ("rcdtParseSeek", "(parse, offset) cursor = chunk data + offset"),
    0x92C0F0: ("rcdtParseAddDelimiters", "(parse, chars) marks each byte of the string as a delimiter. rcdtRsdLoad adds '[]='"),
}

# ---------------------------------------------------------------------------------------------
# Functions whose current name is something specific
# ---------------------------------------------------------------------------------------------

RENAMES = {
    # positional names whose module has a digit (rcdtTm2, rcdtRsd2), which the placeholder pattern
    # does not match, so they are renamed by their exact current name
    0x9232A0: ("rcdtTm2_LoadFile", "was rcdtTm2__f9232A0. Static (obj, path, convertAlpha). rcdtChunkLoad, then copies the 16 byte file header and the first 0x30 byte picture header, accepts image type 4 or 5 only (else 'Image Type Error'), reads w*h/2 or w*h pixel bytes at 16+headerSize and clutSize bytes at 16+headerSize+imageSize. Never checks the TIM2 magic. DEAD in retail, RCDATA.md 4.1"),
    0x923B40: ("rcdtTm2New", "was rcdtTm2__f923B40. Allocates and inits a 0x50 byte TIM2 object"),
    0x9243C0: ("rcdtRsdLoad", "was rcdtRsd2__f9243C0. Exact name, its own 'rcdtRsdLoad : File not found.' message. (rsd, path) text mode, delimiters whitespace plus '[]=', '@' and '#' lines skipped. Keywords PLY MAT GRP NTEX TEX VGR: PLY and MAT load the lowercased file from the .rsd's directory, GRP and VGR are read and dropped, NTEX allocates +28, TEX[i] stores a name. DEAD in retail, RCDATA.md 5"),
    0x924750: ("rcdtRsdNew", "was rcdtRsd2__f924750. Allocates and inits a 32 byte RSD object"),
    0x92B810: ("rcdtChunkGetData", "was unknown_libname_656. A FLIRT false positive, the body is return chunk->data. LIVE through rcdtParseSetChunk"),
    0x92BA50: ("rcdtParseDelete", "was j_j_j_FFX_MemFreeLocked_2_6. Final is empty so Delete is a plain free. Every caller frees a cursor made by rcdtParseNew"),
    0x92BB20: ("rcdtParseGetChunk", "was unknown_libname_657. A FLIRT false positive, the body is return parse->chunk. Orphan, no reference of any kind"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xC5E754: ("g_rcdtTm2PsmByImageType", "TIM2 image type -> GS PSM, 6 dwords {0, 2, 1, 0, 0x14, 0x13} for types 0..5. 0xC5E76C after it is a separate global with its own readers. Indexed by the file's u8 with no bound check. Read by rcdtTm2_GetPsm only"),
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
