"""
Apply the command line census names from reversing/COMMAND_LINE.md to the FFX.exe IDB.

FFX.exe reads its command line once, in WinMain (GetCommandLineW -> CommandLineToArgvW -> a narrow
argv), and hands that argv to Phyre__PApplication__parseCommandLine for the 13 dash flags and to
the game for the argv[1] tokens "debug", "DEBUG" and "_ECalm". Mapping every reader turned up the
PhyreEngine input log and script harness behind -script=, -log= and -input= (the recorder, the
loader, the 16 script commands), an orphan single argument copy of the parser, the debug user name
that argv[1] feeds, the CRT's own argc/argv that nothing reads, and the dead environment variable
readers. Addresses are IDA VAs.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all (the orphan parser at 0x62B150,
the getter at 0x623D10 and the two orphan environment readers at 0x42FF80 and 0x430F80 have no
function and no label). A name starting maybe_ is a guess and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_command_line_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the parser and its getters
    0x62B150: ("Phyre__PApplication__parseOneArgument_ORPHAN", "a single argument copy of Phyre__PApplication__parseCommandLine (thiscall, help byte in ecx, (arg, display block), retn 8). No function, no code or data reference anywhere. It is why every flag string has two xrefs. COMMAND_LINE.md 2"),
    0x623D10: ("Phyre_GetFixedTimeStepArg", "fld g_phyreFixedTimeStepArg, retn. No caller"),
    0x623D40: ("Phyre_GetInputReplayPathArg", "returns g_phyreInputReplayPathArg (-input=). No caller"),
    0x623D90: ("Phyre_GetInputLogPathArg", "returns g_phyreInputLogPathArg (-log=). No caller"),
    0x623E90: ("Phyre_GetScriptPathArg", "returns g_phyreScriptPathArg (-script=). No caller"),
    # the frame drivers
    0x62C6F0: ("Phyre__PApplication__scriptFrameTick", "the app+0x268 frame callback onInit installs instead of frameTick when -script= or -input= produced a script. Returns 9 on quit, resizes on frame 0, bumps app+0x34, runs one script command through Phyre__PApplicationScript__step"),
    0x627480: ("Phyre__PApplicationScript__step", "one script step. While a Frames(n) count is pending it calls the Frames handler, else it parses the next Name(args) command and dispatches it by _stricmp over the 72 byte {fn, name[64]} rows. Frames(n) itself only stores n. 17 on an unknown or badly formed command"),
    0x6276E0: ("Phyre__PApplicationScript__parseCommand", "splits the next 'Name(args)' out of the script text, NUL terminates both. 'Badly formed command %s'"),
    0x61FC20: ("Phyre__PApplicationScriptBuffer__ctorFromFile", "reads the whole -script= file through the fios stream reader into the buffer. 'Unable to get open reader stream to file'"),
    0x61FAC0: ("Phyre__PApplicationScript__ctorFromText", "builds a PApplicationScript from an in-memory string. onInit uses it for the synthesized -input= script 'InputSource(%s)\\nFrames(%06i)\\n'"),
    # the input log
    0x629E40: ("Phyre__PApplication__recordInputLog", "-log= recorder, called by frameTick before Phyre__PInputMapper__latchDeviceStates when app+0x29C is set. Walks only the 7 keyboard slots at mapper+0x80, semantics 28..283, and writes 'F:IK:action:+|-:v' for a mapped key and 'F:KB:dev:key:v' otherwise, F = app+0x34. Pads (mapper+0x38) and analog axes are never recorded"),
    0x62B5F0: ("Phyre__InputLog_LoadInputSource", "parses an input log file into g_phyreInputLogEntries (144 byte rows {type, frame, ...}): KB rows type 1, IK rows type 2, any other identifier that names a script command type 3 (an embedded command such as Timestep). '#' starts a comment"),
    0x626070: ("Phyre__InputLog_ApplyEntry", "applies one log row: KB writes the keyboard state byte at device+24+key, IK writes the mapped key's pressed or released byte, type 3 runs the named script command with its argument text"),
    0x6285B0: ("Phyre__InputLog_GetLastFrameNumber", "reads a log file and returns the frame number of its last 'F:' line (19 when the file will not open). onInit uses it + 1 for the Frames() count of the synthesized -input= script"),
    0x62BB60: ("Phyre__InputLog_RegisterCommands", "registers the 16 script commands: Frames Timestep Framerate MinFramerate RenderFrame Keypress KeyDown KeyUp Buttonpress ButtonDown ButtonUp Mousepress InputSource Expect Echo Quit"),
    0x62BAB0: ("Phyre__InputLog_RegisterCommand", "appends a 72 byte {fn, name[64]} row to the script command table, growing it by doubling (minimum 16)"),
    0x625720: ("Phyre__InputLog_CmdTimestep", "Timestep(f): app+0x3C = f. f <= 0 rebases the clock. A -log= file starts with '0:Timestep:%f', which replays through here"),
    0x6235D0: ("Phyre__InputLog_CmdFramerate", "Framerate(label): prints 'Framerate:%s: %f %d' with app+0x30 and app+0x34"),
    0x624900: ("Phyre__InputLog_CmdMinFramerate", "MinFramerate(): returns 0, does nothing"),
    0x624D70: ("Phyre__InputLog_CmdRenderFrame", "RenderFrame(file): renders (app vtbl+32) and writes a screenshot through PApplication__screenshot"),
    0x624890: ("Phyre__InputLog_CmdKeypress", "Keypress(c): keyboard slot 0 key byte_C30E08[c & 0x7F] down, app vtbl+28 update, key up"),
    0x624810: ("Phyre__InputLog_CmdKeyDown", "KeyDown(c): keyboard slot 0 key byte_C30E08[c & 0x7F] = 1"),
    0x624850: ("Phyre__InputLog_CmdKeyUp", "KeyUp(c): keyboard slot 0 key byte_C30E08[c & 0x7F] = 0"),
    0x623060: ("Phyre__InputLog_CmdButtonpress", "Buttonpress(name): pad slot 0 (mapper+0x38) button down, app vtbl+28 update, release through Phyre__InputScript__setPadButtonByName. Names Circle Cross Triangle Square Start Select Up Down Left Right only, no shoulders, no sticks. No pad bound -> no effect"),
    0x622D40: ("Phyre__InputLog_CmdButtonDown", "ButtonDown(name): pad slot 0 button state = 1, same ten names as Buttonpress"),
    0x622ED0: ("Phyre__InputLog_CmdButtonUp", "ButtonUp(name): pad slot 0 button state = 0, same ten names as Buttonpress"),
    0x624910: ("Phyre__InputLog_CmdMousepress", "Mousepress(x,y): mouse button and position in the mapper's mouse block for one app vtbl+28 update"),
    0x624410: ("Phyre__InputLog_CmdInputSource", "InputSource(file[,...]): the text before a comma goes to Phyre__InputLog_LoadInputSource"),
    0x623460: ("Phyre__InputLog_CmdExpect", "Expect(): returns 0, does nothing in this build"),
    0x623310: ("Phyre__InputLog_CmdEcho", "Echo(text): prints text"),
    0x624D30: ("Phyre__InputLog_CmdQuit", "Quit(): app+0x39 = 1, the quit request"),
    0x9F0810: ("Phyre__PStreamWriterFile__create", "allocates a PStreamWriterFile on a fios handle opened for write. onInit's -log= writer"),
    0x9F06A0: ("Phyre__PStreamWriter__write", "thiscall (data, size) through vtbl+20, adds size to +0x0C. 9 on failure"),
    # the debug user name argv[1] feeds
    0x821D30: ("FFX_Dbg_SetUserName", "g_ffxDbgUserName = a copy of a1. Only caller FFX_InitSubsystems1, in debug mode, with argv[1], so the name is always 'debug' or 'DEBUG'"),
    0x820610: ("FFX_Dbg_GetUserName", "returns g_ffxDbgUserName, the PS2 dev user name used in host0:/home/%s/... paths and the 'yonishi' check in sub_7FF960"),
    0x820620: ("FFX_Dbg_GetAssetPathUserName", "'sugimoto' when g_ffxDbgUseSugimotoUser, else g_ffxDbgUserName. The $USER$ value for FFX_Asset_ResolvePathWithDebugOverrides"),
    0x820640: ("FFX_Dbg_GetUseSugimotoUser", "returns g_ffxDbgUseSugimotoUser, which FFX_InitSubsystems1 clears and nothing sets"),
    # environment variable readers
    0x42FD10: ("Phyre_GetEnvVar", "GetEnvironmentVariableA wrapper, 0 ok, 5 missing, 0x11 too small. Only caller Phyre_ExpandEnvVarsInPath_ORPHAN"),
    0x60FB00: ("Phyre_ExpandEnvVarsInPath_ORPHAN", "expands %VAR% in a path through Phyre_GetEnvVar. No caller"),
    0x42FF80: ("Phyre_ReadScePhyreEnv_ORPHAN", "reads SCE_PHYRE into a 0x103 byte buffer. No function, no caller"),
    0x430F80: ("Phyre_AddCgBinPathToPath_ORPHAN", "reads CG_BIN_PATH and appends it to PATH through Phyre_AppendToPathEnv. No function, no caller"),
    0x431080: ("Phyre_AppendToPathEnv", "_dupenv_s PATH, appends ';' + the old value to a1 and _putenv_s's it. Only caller Phyre_AddCgBinPathToPath_ORPHAN"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x62C730: ("Phyre__InputScript__setPadButtonByName", "was Phyre__InputScript__buttonFromName. It does not map a name to a button, it writes a3 into pad slot 0's state byte for that name (Circle Cross Triangle Square Start Select Up Down Left Right). Buttonpress calls it with 0 to release"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xC494A8: ("g_ffxDbgUserName", "char*, default the empty string 0xB8FBEB. Debug mode sets it to a copy of argv[1] (FFX_Dbg_SetUserName from FFX_InitSubsystems1), so it is 'debug' or 'DEBUG' or empty, never a real user"),
    0x12FB8F8: ("g_ffxDbgUseSugimotoUser", "byte, FFX_InitSubsystems1 clears it at 0x820332 and nothing sets it, so FFX_Dbg_GetAssetPathUserName never returns 'sugimoto'"),
    0x193E9D0: ("g_crtArgc", "the CRT's argc from __getmainargs in _pre_cpp_init. Never read, the game parses GetCommandLineW itself"),
    0x193E9D4: ("g_crtArgv", "the CRT's argv from __getmainargs. Never read"),
    0x193E9D8: ("g_crtEnvp", "the CRT's envp from __getmainargs. Never read"),
    0xCC9CF8: ("g_phyreInputLogCursor", "index of the next g_phyreInputLogEntries row Phyre__InputLog_CmdFrames applies"),
    0xCC9D14: ("g_phyreInputLogEntryCount", "PArray count (low 31 bits) of g_phyreInputLogEntries, filled by Phyre__InputLog_LoadInputSource"),
    0xCC9D18: ("g_phyreInputLogEntries", "144 byte rows {type, frame, payload} loaded from an InputSource() log. Freed when the cursor passes the end"),
    0xCCA310: ("g_phyreWindowRecord", "the record app+0x264 points at, built by createWindowAndInit: +0 hwnd, +4 the constant 1 (the swap chain sample count), +8 app+0x298 fullscreen, +9 app+0x288, which FFX_Phyre_CreateDeviceAndSwapChain stores as the initial vsync byte renderer+0x131C at 0x595AE6"),
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
