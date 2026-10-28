"""
Apply the effect overlay names from reversing/EFFECT_OVERLAY.md to the FFX.exe IDB.

The overlay is the 16 slot function table every magic DLL returns from GetEffectOverlayTable(0).
FFX.exe keeps it in actor+0xF8C for a battle unit's spell and in g_ffxGlobalEffectOverlayTable for
the one global effect, and walks a byte state machine at actor+0xDFB (g_ffxGlobalEffectState for the
global one):

    1 begin, 16 async load pending, 14 retry
    2 -> 3 the cast motion, 3 waits on the load flag actor+0xDFD, voice, explode
    3 installs slot 7 (an ATEL script blob) and goes to 11, or 12 for an overdrive minigame
    11 -> 4 calls slot 0 start(handle, 2, 0)
    4 -> 5 FFX_Btl_EffectSetReady, host slot 226, called by DLL code. Starts the hit reactions
    5 -> 6 FFX_Btl_EffectAdvanceState, host slot 214, called by DLL code
    6 FFX_Btl_FinishActions applies the pending hits, calls slot 1 free(0, 0), state 0

Slot 3 runs once per sub-step and slot 4 once per effects pass, from FFX_Magic_CallOverlayStep in
FFX_MainStep, both as fn(0) cdecl with the return ignored. The exe has its own resident table,
g_ffxResidentEffectOverlayTable, whose slots 3 and 4 run first every time.

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

    python apply_overlay_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # the resident overlay table at 0xC48D90, 32 dwords, the exe's own effect unit 0 program
    0x800580: ("FFX_MagicResident_OvlInit", "resident overlay slot 0. Writes the resident table into effect unit 0 +0x50 (off_C88A90+80). Called once by FFX_Rand_SeedCosmeticStreams at boot, never by the battle code"),
    0x8005F0: ("FFX_MagicResident_OvlFreeStub", "resident overlay slot 1, returns 0"),
    0x800600: ("FFX_MagicResident_OvlSlot2Stub", "resident overlay slot 2, returns 0"),
    0x800610: ("FFX_MagicResident_OvlStep", "resident overlay slot 3, FFX_Magic_CallOverlayStep(0) calls it first every sub-step: FFX_Magic_Oef2Dispatch(0, 0)"),
    0x800670: ("FFX_MagicResident_OvlEffectsPass", "resident overlay slot 4, FFX_Magic_CallOverlayStep(1) calls it first every effects pass: FFX_Tex_DrainUploadQueue, maybe_FFX_SpuStream_ServiceRequest, camera matrices, Oef2Dispatch(0, 1), opp_main(0). The texture drain is what releases host 313 waits in DLL slot 3"),
    0x8006F0: ("FFX_MagicResident_OvlSlot15Stub", "resident overlay slot 15, returns 0"),
    0x800700: ("FFX_MagicResident_HdrRelocateParticles", "resident header +0x40: FFX_Vfx_RelocateParticleSet on the object's set. No exe reader of header +0x40 found"),
    0x800770: ("FFX_MagicResident_HdrAllocPrimList", "resident header +0x44: claims one of 256 prim list slots (dword_12E9C70) for the object, stores it at obj+188"),
    0x8008B0: ("FFX_MagicResident_HdrStepPrimList", "resident header +0x48: sub_712250 clock step, frees the list when it reports done. Frozen by g_ffxVfxTimeFrozen"),
    0x800960: ("FFX_MagicResident_HdrStub4C", "resident header +0x4C, returns 0"),
    0x800970: ("FFX_MagicResident_HdrFreeAllPrimLists", "resident header +0x50: frees all 256 prim list slots"),
    0x8009C0: ("FFX_MagicResident_HdrFreeObjectPrims", "resident header +0x54: frees obj+188 when obj+187 is 3. Called through unit+0x50 +0x54 by sub_8014C0 at 0x801661"),
    0x800A30: ("FFX_MagicResident_HdrOef2Pass2", "resident header +0x74: FFX_Magic_Oef2Dispatch(0, 2). Registered as a VFX callback by the Oef2 opcode sub_80B180 at 0x80B3F2, so it runs inside FFX_Vfx_EndPass"),
    0x80B180: ("maybe_FFX_Oef2_OpRegisterHdrCallback", "opcode table 0xC490C8: reads unit+0x50 header +0x74 and registers it as a VFX callback, group chosen by the op word (1 is group 32 through FFX_Vfx_AddEffectCallback)"),

    # the battle side state machine
    0x788480: ("FFX_Btl_StepMagicEffectStates", "from FFX_Btl_MainStep, every battle step: the actor+0xDFB effect state machine for actors 0..30. State 3 installs overlay slot 7, 11 calls slot 0 and goes to 4, 5 runs FFX_Btl_StepEffectReactions"),
    0x787AF0: ("FFX_Btl_FreeActorMagicEffect", "from FFX_Btl_FinishActions once the hits are applied: overlay slot 1 free(0, 0), op_ovl_effect_free, MagicFile__stop, actor+0xDFB = 0"),
    0x788DF0: ("FFX_Magic_PackEffectHandle", "(counter, kind, unit) -> (counter & 0xFFF) << 16 | kind << 8 | unit. kind 2 battle unit, 0x80 the global effect. The handle slot 0 receives"),
    0x788EF0: ("FFX_Btl_InstallMagicEffectScript", "battle state 3: overlay slot 7 of actor+0xF8C (or dword_1133368 when set) -> FFX_BtlEv_LoadEffectScriptBlob"),
    0x797550: ("FFX_BtlEv_LoadEffectScriptBlob", "blob+[4] -> FFX_BtlEv_AddScriptBlock(2, 0, ..) and FFX_BtlEv_BuildScriptContext(2), blob+[8] -> dword_1135DF8 the event table. So overlay slot 7 is ATEL script, simulation"),
    0x788F90: ("FFX_Btl_StartEffectReactions", "from FFX_Btl_EffectSetReady and state 10: starts the target reaction motion, then FFX_Btl_StepEffectReactions"),
    0x789030: ("FFX_Btl_StepEffectReactions", "state 5, every battle step: per target hit reactions, overdrive gain, damage numbers. Sets actor+0xE0D = 1 when all are done, which FFX_Btl_GetEffectHitsDone returns to the DLL"),
    0x7881B0: ("FFX_Btl_GetMagicPrxActor", "FFX_Battle_GetActor(g_ffxMagicPrxActor)"),
    0x7881C0: ("maybe_FFX_Btl_AbortMagicPrxLoad", "clears actor+0xDFD and actor+0xF8C of g_ffxMagicPrxActor, or the global effect state when no battle effect is pending"),

    # the global effect, kind 0x80
    0x788B90: ("FFX_Magic_ForceEndGlobalEffect", "'EFFECT SYNC ERROR' assert when a global effect is still live, then state 6 and FFX_Magic_FreeGlobalEffect"),
    0x788BE0: ("FFX_Magic_ClearGlobalEffect", "zeroes g_ffxGlobalEffectState, g_ffxGlobalEffectOverlayTable and g_ffxGlobalEffectHandle"),
    0x788C00: ("FFX_Magic_FreeGlobalEffect", "state 6 only: overlay slot 1 free(0, 0), close the camera handles, op_ovl_effect_free, MagicFile__stop, restore the keep speed"),
    0x788D60: ("FFX_Magic_StartGlobalEffectOverlay", "state 3 only: handle kind 0x80 from g_ffxGlobalEffectCounter, state 4, overlay slot 0 start(handle, 2, 0)"),

    # host API slots the overlay code calls
    0x7FCE10: ("FFX_Magic_GetEffectHoldFlag", "host slot 225: -1 while g_ffxEffectHoldFlag is set, which Oef2 sync op cases 1 and 2 set and clear"),
    0x7EB300: ("FFX_Vfx_RemoveEffectCallback", "host slot 311: removes a callback from group 32"),
    0x7EB320: ("FFX_Vfx_AddEffectCallback", "host slot 312: registers fn into callback group 32, which FFX_Vfx_EndPass runs inside the effects pass"),
    0x7E46D0: ("FFX_Tex_IsUploadQueued", "host slot 313: -1 while any queued upload matches the mask. The queue drains at 0x100000 bytes per effects pass, so the wait is step counted, not timed"),
    0x7E4720: ("FFX_Tex_DrainUploadQueue", "from FFX_MagicResident_OvlEffectsPass, 0x100000 byte budget per call"),
    0x8000F0: ("maybe_FFX_SpuStream_PollRequest", "host slot 549: nonzero while the PS2 era stream request is pending. On PC sub_A44770 returns -1 so it clears after one resident slot 4 pass"),
    0x800170: ("maybe_FFX_SpuStream_ServiceRequest", "from FFX_MagicResident_OvlEffectsPass"),
    0x72A750: ("FFX_Vfx_SetTimeFrozen", "g_ffxVfxTimeFrozen = a1. From MapFx_102 SetVfxTimeFrozen and sub_9325C0"),
    0x71E610: ("maybe_FFX_Vfx_IsOverlaySpecialMagic", "draw side: 1 for magic ids 307, 605, 106, 281, 563, 612, 522, 445, 162, 586 and 14 while g_ffxInMagicOverlayCall is set"),

    # Oef2 opcode handlers that move the state machine
    0x816680: ("FFX_Oef2_OpEffectSync", "opcode table 0xC491C8. Cases 1 and 2 set and clear g_ffxEffectHoldFlag, 3 and 5 wait, 4 and 6 FFX_Btl_EffectSetReady(h, 1 or 0), 7 and 8 map, chr and meswin draw enables"),
    0x815D90: ("FFX_Oef2_OpApplyPendingHits", "opcode table 0xC48F50: sub_78D980, which applies pending HP, MP and CTB hits at once"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x787CA0: ("FFX_Btl_GetEffectHitsDone", "was maybe_FFX_Btl_GetUnitEffectSlot. Host slot 227. Returns actor+0xE0D for kinds 2, 4 and 7 and 1 for 0x80. FFX_Btl_StepEffectReactions sets +0xE0D when every target reaction has finished, and DLL slot 3 or 4 waits for it before advancing"),
    0x788200: ("FFX_Btl_EffectSetReady", "was maybe_FFX_Btl_EffectSetReady. Host slot 226. Verified: cmp byte [esi+0DFBh], 4 at 0x788236, mov byte [esi+0DFBh], 5 at 0x788261, between them FFX_Btl_StartEffectReactions. a2 picks the command's reaction motion (1) or motion 51 (0)"),
    0x788920: ("FFX_Btl_EffectAdvanceState", "was maybe_FFX_Btl_EffectAdvanceState. Host slot 214. Verified: kind 2 needs state 5 (cmp at 0x78895F, else 'effect does not ready call') and writes 6 at 0x78897A. Kind 0x80 writes g_ffxGlobalEffectState = 6"),
    0x787AB0: ("FFX_Magic_EndPhaseOverlayFree", "was maybe_FFX_Magic_OverlayFree. From FFX_Btl_EndPhaseStep with word_112C94E. Only 671 calls overlay slot 8 (handle, 2, 0, 0), anything else is sub_7FE6C0(-1) then op_ovl_effect_free"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x1340870: ("g_ffxMenuSysGsBackupBuffer", "was g_ffxBuiltinEffectOverlayTable. Not an overlay table. A 5 MB buffer FFX_MenuSys_RunFrame memsets with 0x500000 and sub_8AA690 and sub_8AA6D0 fill with 13 x 64 KB GS reads. The magic code stores its ADDRESS in actor+0xF8C and the global table while a DLL loads, only as a nonzero marker. It is never called, because FFX_Magic_CallOverlayStep needs state 4 or 5 and state 3 waits for the real table. The 0xFF bytes IDA shows are uninitialized data"),
    0xC48D88: ("g_ffxResidentEffectOverlayTablePtr", "-> g_ffxResidentEffectOverlayTable. Read by FFX_Magic_CallOverlayStep for slot 3 and 4 and by FFX_MagicResident_OvlInit"),
    0xC48D8C: ("maybe_g_ffxResidentEffectOverlayUpperPtr", "-> table+0x40, no code reader"),
    0xC48D90: ("g_ffxResidentEffectOverlayTable", "the exe's own overlay header, 32 dwords, installed in effect unit 0 +0x50. Slots 0..15 are the GetEffectOverlayTable shape. +0x40..+0x54 and +0x74 are the particle and Oef2 callbacks the effect unit code reads through unit+0x50 (+0x54 at 0x801661, +0x60 at 0x81715C, +0x74 at 0x80B3F2, +0x78 at 0x80C222, +0x7C at 0x80D566)"),
    0x1133358: ("g_ffxGlobalEffectCounter", "the counter field of the next global effect handle, bumped by FFX_Magic_StartGlobalEffectOverlay"),
    0x1133360: ("g_ffxGlobalEffectOverlayTable", "the global effect's overlay table, the twin of actor+0xF8C. Set from MagicFile__getEffectOverlayTable at 0x788D3E"),
    0x1133364: ("g_ffxGlobalEffectState", "byte, the global effect's state, the twin of actor+0xDFB"),
    0x113336C: ("g_ffxGlobalEffectHandle", "the handle passed to overlay slot 0 and slot 8, kind 0x80"),
    0x113337C: ("g_ffxMagicPrxActor", "the battle unit whose magic DLL is loading, cleared by FFX_Btl_StartMagicPrx"),
    0x11A0061: ("g_ffxEffectHoldFlag", "byte, set and cleared by FFX_Oef2_OpEffectSync cases 1 and 2, read through host slot 225"),
    0x11A03A0: ("g_ffxTexUploadQueue", "64 entry ring of upload masks, head and count in dword_11A04A0. Host slot 313 polls it, FFX_Tex_DrainUploadQueue drains it"),
    0x23C3648: ("maybe_g_ffxSpuStreamRequest", "two dword request states, 0 idle, -1 waiting on sub_885EE0, other values in flight"),
    0x230FD34: ("g_ffxVfxTimeFrozen", "host data slot 758. Nonzero stops the particle and curve clocks (sub_712250 and about 140 more readers). DLLs write it as (FFX_Player__getSubStepCount() == 0), so effect time does not advance on a step with no sub-steps. Exe writers FFX_Vfx_SetTimeFrozen, sub_760080 cases 19 and 20, sub_761D60"),
    0x112C94E: ("maybe_g_ffxBtlEndPhaseMagicId", "word, the global effect id the end phase starts and frees. 670 on game over. FFX_Magic_EndPhaseOverlayFree calls slot 8 only for 671. Set by FFX_Btl_BeginBattle and Btl_284"),
    0x1133368: ("maybe_g_ffxBtlEffectScriptOverride", "when nonzero FFX_Btl_InstallMagicEffectScript uses it instead of overlay slot 7. Only zeroing writes found"),
    0x1135DF4: ("g_ffxBtlEvEffectScript", "blob+[4] of the installed overlay slot 7 script"),
    0x1135DF8: ("g_ffxBtlEvEffectEventTable", "blob+[8] of the installed overlay slot 7 script, read by FFX_BtlEv_StartFirstMatch and FFX_BtlEv_FindSlotRecord"),
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
