"""
Apply the map particle names from reversing/MAP_PARTICLES.md to the FFX.exe IDB.

The field placed particle set is MAP1 section 10. Its glue is the MapFx code at 0x728A60..0x72AC2F
(load, release, the group API, the frustum cull, the record transforms, the point light commit),
and it runs on the ppp core at 0x7120F0..0x717400, which the magic PM effects share. This names
the glue from its positional MapFx__f names, the ppp core functions the map path reaches, the
pppDrawRain helpers (the Thunder Plains rain), and the globals of the placed set. Two MapFx syscall
rows are renamed: 093 restarts rather than kills, and 054 had its sense backwards like 013.

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

    python apply_mapparticle_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # MapFx glue, the placed set (MAP_PARTICLES.md 2, 4, 5)
    0x728AC0: ("FFX_MapFx_BuildRecordMatrix", "per placed record: rotation order (+75), scale, attach to a CHR joint (x1000), map part or map node from the attach record, then the view mode (+74). From FFX_MapFx_UpdatePlacedEffects 0x729CB7"),
    0x728DD0: ("FFX_MapFx_OutCode", "frustum outcode of a projected point: bit 1 x < -0.889, 2 x >= 0.889, 4 y < -0.5, 8 y >= 0.5"),
    0x728E40: ("FFX_MapFx_UnlinkSlotMeshes", "unlinks the dynamic meshes of a suspended slot. Called when attach flag 0x10 is set (0x729B65)"),
    0x728F10: ("FFX_MapFx_FrustumCull", "(view, pos, far, halfW, halfH). 0 visible, and always 0 when far < 0. 15 past far or behind, else the AND of four corner outcodes. Only group 0 records are culled, and a culled record pauses. MAP_PARTICLES.md 5.2"),
    0x729050: ("FFX_MapFx_SetActive", "g_ffxMapFxActive = a1, the first gate of FFX_MapFx_UpdatePlacedEffects. MapFx 12, core 550, save 54"),
    0x729060: ("FFX_MapFx_DebugDump", "prints every placed record through the stubbed printf, does nothing in retail"),
    0x7291C0: ("FFX_MapFx_ReleaseSet", "frees the placed set: fp effect free, every slot's prim list, the extra heap, the packet buffer and the point light pool. From FFX_Map_UnloadMapOut and the dev loader"),
    0x729280: ("FFX_MapFx_GetHeapPeak", "returns g_ffxMapFxHeapPeak, debug meter"),
    0x729290: ("FFX_MapFx_GetHeapSpan", "the two ppp heap spans (end - start + 16) summed, debug meter, also read by FFX_Vfx_StepAll"),
    0x7292D0: ("FFX_MapFx_GetHeapUsed", "returns g_ffxMapFxHeapUsed, debug meter"),
    0x7292F0: ("FFX_MapFx_FindGroupSlot", "(group, &slot, &attach): the first placed record whose group (+68) matches, both outs 0 when none. Used by the rcBg FP getters"),
    0x7293D0: ("FFX_MapFx_GetPacketPeak", "returns g_ffxMapFxPacketPeak, debug meter"),
    0x7293E0: ("FFX_MapFx_GetPacketSize", "returns g_ffxMapFxPacketSize, debug meter"),
    0x7293F0: ("FFX_MapFx_GetPacketUsed", "returns g_ffxMapFxPacketUsed, debug meter"),
    0x729400: ("FFX_MapFx_SuspendGroup2", "sets attach flag 0x10 on every record whose second group word (+70) matches. MapFx 54 with 0"),
    0x729440: ("FFX_MapFx_ResumeGroup2", "clears attach flag 0x10 on every record whose +70 matches. MapFx 54 with nonzero"),
    0x729480: ("FFX_MapFx_KillGroup2", "frees every record whose +70 matches and marks it dead (-4096). MapFx 96"),
    0x7294F0: ("FFX_MapFx_StopGroup2", "slot +29 = 1 (stopping) on every record whose +70 matches. MapFx 53 with 0"),
    0x729530: ("FFX_MapFx_RestartGroup2", "slot +0 = 0 and +29 = 0 on every record whose +70 matches, so it restarts next pass. MapFx 53 with nonzero"),
    0x7295F0: ("FFX_MapFx_KillGroup", "frees every record of the group and marks it dead (-4096). MapFx 95"),
    0x729660: ("FFX_MapFx_StopGroup", "slot +29 = 1 (stopping) on every record of the group, it finishes its cycle and frees. MapFx 2 with 0, rcbgFP"),
    0x7296A0: ("FFX_MapFx_KillAllGrouped", "frees every record with a nonzero group and marks it dead. MapFx 8"),
    0x729740: ("FFX_MapFx_RestartAllGroups", "slot +0 = 0, +29 = 0 for every grouped record, a restart. MapFx 7"),
    0x7297E0: ("FFX_MapFx_ZeroAllCountdowns", "slot +0 = 0 on every record, dead ones included, which RESTARTS them all on the next pass. MapFx 93"),
    0x729810: ("FFX_MapFx_StartGroupOneShot", "slot +0 = 0 and +29 = 2: restart, and the restart turns 2 into 1 so it plays one cycle and stops. MapFx 3"),
    0x729850: ("FFX_MapFx_DebugDrawCamPlayer", "debug text CAM, PLY and DIS (camera to player distance). From FFX_Vfx_StepAll"),
    0x7299E0: ("FFX_MapFx_RegisterHeapMeter", "registers the ppp heap with the debug memory meter and sets dword_CEC218. From FFX_Vfx_StepAll until done once"),
    0x729A10: ("FFX_MapFx_RestoreGroupStates", "re-applies the saved running and suspended bits of each group after a reload. From FFX_Field_MapRegLoadTask and sub_8615B0"),
    0x729F70: ("FFX_MapFx_ResetPointLights", "point light count and mask = 0. Also at the start of FFX_Vfx_StepAll"),
    0x729F90: ("FFX_MapFx_CommitPointLights", "grows the light pool to the count, disables the unused tail, writes the mask into +0x188 of every live CHR. Runs after the map pass and again at the end of FFX_Vfx_StepAll. MAP_PARTICLES.md 7"),
    0x72A120: ("FFX_MapFx_UnbindGroup", "clears the attach type bits (keeps 0x10) and target of the group's first record. MapFx 90, actor release"),
    0x72A180: ("FFX_MapFx_SaveGroupStates", "records each group's running and suspended bits into dword_1326BE0. From maybe_FFX_Atel_ReleaseAllActorChrs"),
    0x72A260: ("FFX_MapFx_ApplyViewMode", "record +74: 0 view x M, 1 camera aligned, 2 the shared basis"),
    0x72A2E0: ("FFX_MapFx_RecordRotScaleMatrix", "record +75 rotation order: 0 XYZ, 1 XZY, 2 YXZ, 3 YZX, 4 ZXY, 5 ZYX, else XYZ, then scale"),
    0x72A530: ("FFX_MapFx_BindGroupToChr", "(group, chr, joint): attach flag 1. Refuses a CHR that is not live. MapFx 9, 88, rcbgFP"),
    0x72A590: ("FFX_MapFx_BindGroupToMapPart", "(group, part): attach flag 2. MapFx 9, 89"),
    0x72A5E0: ("FFX_MapFx_BindGroupToMapNode", "(group, node): attach flag 8. MapFx 9, 105"),
    0x72A630: ("FFX_MapFx_SetGroupPos", "identity matrix and a position in the group's attach record. MapFx 10, 91"),
    0x72A690: ("FFX_MapFx_BuildSharedBasis", "the basis view mode 2 records share, built once per pass when g_ffxMapFxNeedSharedBasis"),
    0x72A760: ("FFX_MapFx_GetGroupState", "-1 no such group or no set, 0 dead, 1 running, 2 stopping or one shot, from the group's first slot. MapFx 5 poll"),
    0x72AB20: ("FFX_MapFx_VecLength", "sqrt(x*x + y*y + z*z) plus a constant, through scratch globals"),
    # ppp core, shared with the magic PM effects (MAP_PARTICLES.md 5.3)
    0x7120F0: ("FFX_Ppp_RelocateProgram", "turns a program's offsets into PrxPtrs, links op entries to g_ffxPppOpTable rows, stops at the node whose next points at a zero dword"),
    0x712250: ("FFX_Ppp_StepPart", "one pass of a slot without the alloc failure check: PrePass, births, FFX_Ppp_CalcPass, PostPass, then the clock unless frozen. Returns true when done. FFX_Vfx_StepAll"),
    0x7122F0: ("FFX_Ppp_StepPartChecked", "FFX_Ppp_StepPart with g_ffxPppAllocFailed ending the slot. The map path, 0x729DB0"),
    0x7123C0: ("FFX_Ppp_StartPart", "(slot, dt, program, runInit): fills the 128 byte slot, allocates node states, colour scale 4096, mode 0, then Init hooks"),
    0x712B80: ("FFX_Ppp_BirthNodes", "one particle per node whose birth time <= slot time, then that birth time = 0x7FFFFFFF"),
    0x712D20: ("FFX_Ppp_ReConParticle", "resets the key pointers and calls every entry's ReCon column, at a node loop"),
    0x712D90: ("FFX_Ppp_ConParticle", "sets the key pointers and calls every entry's Con column, at birth"),
    0x712E00: ("FFX_Ppp_RunPostPass", "PostPass column of every op in the slot's op list, flag 0x20"),
    0x712E40: ("FFX_Ppp_RunInit", "Init column of every op in the slot's op list, from FFX_Ppp_StartPart"),
    0x712E80: ("FFX_Ppp_RunPrePass", "PrePass column of every op in the slot's op list, flag 0x10"),
    0x716770: ("FFX_Ppp_BirthParticle", "(slot, node state): allocates node +32 + 4 x ops from the heap or the extra heap, inserts by node +36 order, then FFX_Ppp_ConParticle. Sets g_ffxPppAllocFailed on failure"),
    0x7168F0: ("FFX_Ppp_FreeParticle", "Des column of every entry, destroys its dynamic geometry, unlinks from the node state, frees from the main or extra heap"),
    0x7169D0: ("FFX_Ppp_InitHeapForSet", "makes a1 the current ppp heap, stores the set's model, shape and vertex list tables at heap +32, +36, +40, then FFX_PppHeap_Init(a1, base, size)"),
    0x716A90: ("FFX_Ppp_HeapFreeBlock", "(heap, extra heap, block): frees the block from the extra heap when it lies above heap +8, else from the main heap"),
    0x716C40: ("FFX_Ppp_CalcPass", "per particle key advance, Calc, Draw unless node flag bit 0, age += 4096 unless frozen, loop or free. No alloc check"),
    0x717010: ("FFX_Ppp_CalcPassChecked", "FFX_Ppp_CalcPass with g_ffxPppAllocFailed checks. The map path"),
    # the rain and its line model (MAP_PARTICLES.md 9)
    0x755430: ("pppDrawRain_BuildLines", "one line per drop, head pos to pos + tail, colour (s16 x u16 scale) >> 19, tail alpha 0, world space, cap 2500, then FFX_Vfx_SubmitModel"),
    0x755A00: ("pppDrawRain_StepDrops", "pos += vel every Draw, not dt scaled, not frozen gated. A drop whose head and tail are both past a face of the cube camera +- R respawns at random (7 effect RNG draws)"),
    0x756320: ("pppDrawRain_CreateDrops", "allocates N 48 byte drops on the ppp heap: pos = camera + SignedScale(R) per axis, vel = base + SignedScale(jitter), tail = normalize(vel) * -(streak + SignedScale(jitter))"),
    0x756600: ("FFX_Vec3_ScaleKeepW", "out.xyz = v.xyz * s, w from the arguments, through the VU scratch registers"),
    0x766F10: ("FFX_Vec3_AddRandomJitter", "v[i] += SignedScale(j[i]) for 3 axes, times Scale(1.0) when a3 != 0"),
    0x766AE0: ("FFX_Vfx_BuildPs2Projection", "a 512 x 416 PS2 style projection from the camera globals at 0x1137008"),
    0x71A0C0: ("FFX_Vfx_FillLineModel", "writes line vertices and colours into a dynamic line model, rgb / 256 and alpha / 256"),
    0x7112D0: ("FFX_Vfx_CreateLineModel", "creates the dynamic line model keyed by particle plus op index"),
    0x714B60: ("FFX_Vfx_CreateLineModelParts", "(key, max lines) the mesh parts of a line model"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x91C4A0: ("FFX_AtelSys_MapFx_093_RestartAllEffectsAndResetLayerFrames_resi", "was FFX_AtelSys_MapFx_093_KillAllEffectsAndResetLayerFrames_resi. FFX_MapFx_ZeroAllCountdowns sets every slot +0 to 0, and the update restarts a slot whose countdown goes negative (0x729C38), so this restarts every effect. MAP_PARTICLES.md 5.2"),
    0x91B7D0: ("FFX_AtelSys_MapFx_054_SetGroup2Active_resi", "was FFX_AtelSys_MapFx_054_SetGroup2Hidden_resi. (group2, active): active != 0 calls FFX_MapFx_ResumeGroup2 (0x91B7F0), else FFX_MapFx_SuspendGroup2. Same backwards sense as row 13, MAP_PARTICLES.md 8"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xD2C258: ("g_ffxMapFxSet", "the loaded placed particle set (MAP1 section 10), 0 when none"),
    0xD2C260: ("g_ffxMapFxSlots", "128 byte runtime slot per placed record. +0 countdown (0xFFFFF000 dead), +8 time, +29 mode. MAP_PARTICLES.md 4.2"),
    0xD30260: ("g_ffxMapFxAttach", "76 byte attach record per placed record. +0 flags 1 CHR, 2 part, 8 node, 0x10 suspended"),
    0xCEC1F0: ("g_ffxMapFxActive", "first gate of FFX_MapFx_UpdatePlacedEffects (0x729AC0). 1 on load"),
    0xD2C25C: ("g_ffxMapFxNeedSharedBasis", "some placed record has view mode 2"),
    0xCEC228: ("g_ffxMapFxFilterQuirk", "1 when the set has exactly 40 records or the level hash is 564, read by pppDrawFilter"),
    0xC3A504: ("g_ffxMapFxCurrentRecord", "placed record index during the update, -1 outside"),
    0xC3A500: ("g_ffxMapFxCurrentGroup", "group of the record being run, read by the Mdl draw ops"),
    0xCEC22C: ("g_ffxMapFxHeap", "the ppp heap the placed set runs in"),
    0xCEC220: ("g_ffxMapFxPacketBuffer", "draw packet buffer, size from header +2"),
    0xCEC200: ("g_ffxMapFxPacketSize", "size of g_ffxMapFxPacketBuffer"),
    0xCEC204: ("g_ffxMapFxPacketUsed", "packet bytes used this pass, debug meter"),
    0xCEC208: ("g_ffxMapFxPacketPeak", "peak of g_ffxMapFxPacketUsed"),
    0xCEC20C: ("g_ffxMapFxHeapUsed", "ppp heap use this pass, debug meter"),
    0xCEC210: ("g_ffxMapFxHeapPeak", "peak of g_ffxMapFxHeapUsed"),
    0xCEC224: ("g_ffxMapFxExtraHeapBlock", "the header +1 x 8 KB extra heap"),
    0xCEC1F4: ("g_ffxVfxPointLightCount", "point lights added this pass by the pppFpPointLight ops"),
    0xCEC1F8: ("g_ffxVfxPointLightCapacity", "size of the point light pool"),
    0xCEC1FC: ("g_ffxVfxPointLightMask", "light bits written into CHR +0x188 by FFX_MapFx_CommitPointLights"),
    0x230FFE4: ("g_ffxPppCurrentPart", "the slot being started or stepped"),
    0x230FD20: ("g_ffxPppCurrentOpIndex", "op index of the entry being run, part of the dynamic geometry key"),
    0x230FD3C: ("g_ffxPppAllocFailed", "set by FFX_Ppp_BirthParticle and the ppp alloc when the heap is full, ends a checked slot"),
    0x230FD2C: ("g_ffxVfxPacketCursor", "packet write cursor, map pass and FFX_Vfx_StepAll"),
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
