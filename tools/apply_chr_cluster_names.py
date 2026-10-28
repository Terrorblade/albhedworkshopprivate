"""
Apply the character cluster names from reversing/CHR_CLUSTER.md to the FFX.exe IDB.

The walk from a character .dae.phyre to the 32 byte instance record CHR+0x830 points at:
ClassCharacter__createInstance 0x63C540 and FFX_Gfx_CreateMeshInstance 0x66F7D0 build one fresh
PMeshInstance per template PMeshInstance of the model cluster, in a gfx context cluster, with its own
world matrix and bounds, and hang the cloth decorator, the character light material clone, the
texture animation entry and the level depth bias on it. The Phyre side names come from the
PhyreEngine class names the reflection data already gives (PMeshInstance, PMeshInstanceBounds,
PCluster, PWorld), each confirmed against the 3.9 body in IDA, not taken from the 3.21 SDK.

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

    python apply_chr_cluster_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # Phyre object model, as FFX_Gfx_CreateMeshInstance uses it
    0x440B70: ("PCluster__allocateInstanceListForCount", "PCluster::allocateInstanceList(count, desc, additional): count * desc+28 into the size taking variant, which this IDB names PCluster__allocateInstanceList 0x440BA0. No constructor runs, the caller constructs in place at list+52 (allocateInstanceAndConstruct). CHR_CLUSTER.md 3"),
    0x441010: ("PCluster__createCount", "PCluster::create(count, desc, additional): refuses a class with descriptor +144 bit 8 ('no default constructor ... PCluster::create()'), else allocates the list and default constructs each object. Returns the first object (list+48). FFX_Gfx_CreateMeshInstance makes the PWorldMatrix with it. CHR_CLUSTER.md 3"),
    0x443250: ("PWorld__addCluster", "this = world. Removes the cluster from its old world (cluster+64), links it into the world's cluster list (cluster+72/+76), sets cluster+64 = world and registers each instance list. gfx+0x7C and gfx+0x9C are the worlds the character instance cluster gfx+0x11E4 is added to. CHR_CLUSTER.md 3"),
    0x443B70: ("PWorld__removeCluster", "this = world. Unlinks the cluster when cluster+64 is this world and unregisters each instance list. Called by PCluster__dtor 0x43FA30 and before every re-add. CHR_CLUSTER.md 3"),
    0x576630: ("PCluster__ctor", "80 byte PCluster (the reflected size) through sub_43F820. FFX_GfxCtx_ctor builds the character instance clusters at gfx+0x11E4 and gfx+0x1234 with it, 0x50 apart. CHR_CLUSTER.md 3"),
    0x43FA30: ("PCluster__dtor", "calls PWorld__removeCluster on the owning world (0x43FA6D). FFX's graphic finalize tears gfx+0x11E4 and gfx+0x1234 down with it. CHR_CLUSTER.md 3"),
    0x4F27E0: ("PMeshInstance__ctor", "PMeshInstance(PMesh&): m_mesh +0 = mesh, m_materialSet +16 = mesh+48 (the default PMaterialSet), the rest zeroed, then PMeshInstance__setMesh. FFX_Gfx_CreateMeshInstance constructs the runtime character instance with it (0x66F868). CHR_CLUSTER.md 3"),
    0x4F8B90: ("PMeshInstance__setMesh", "sizes m_segmentContext (+36 count, +40 data, 36 bytes per PMeshSegment, sub_4F6380 each) and m_currentPose (+8 count, +12 data, 64 bytes per m_defaultPose entry), copies m_defaultPose into m_currentPose (Vectormath_Matrix4_copy 0x4F8C9D), then m_mesh = mesh, m_materialSet = mesh+48. So a fresh character instance starts in the bind pose. CHR_CLUSTER.md 3"),
    0x4FF800: ("PMeshInstanceBounds__ctorFromInstance", "PMeshInstanceBounds(PMeshInstance&): m_worldMatrix +12 = instance+4, m_meshInstance +28 = instance, min and size copied from the instance's old m_bounds or zeroed, then instance.m_bounds = this. CHR_CLUSTER.md 3"),
    0x500C20: ("PMeshInstanceBounds__setMinAndSize", "min to +0..+8, size to +16..+24. FFX_Gfx_CreateMeshInstance copies the template bounds through it (0x66F932). CHR_CLUSTER.md 3"),
    # the instance record and its release
    0x6377A0: ("FFX_Chr_MeshInstanceListIter_Next", "steps a FFX_Chr_MeshInstanceListIter_Seek iterator to the next instance list whose class derives from the target descriptor and loads it, or clears the iterator. CHR_CLUSTER.md 3"),
    0x6A22D0: ("ClassCharacter__meshInstanceListIter_Next", "the copy of FFX_Chr_MeshInstanceListIter_Next that ClassCharacter__buildMeshTables uses. CHR_CLUSTER.md 3"),
    0x6A1320: ("ClassCharacterInstance__addClothBinding", "this = the 32 byte instance record. Scans the mesh instance's PMesh segments (108 byte stride) for one with m_clothmodelIndex +32 != -1 and m_clothIndex +36 != -1 (0x6A1364, 0x6A136A). Any: a 20 byte binding plus Cloth_InstallInstancingMesh. Always pushes the binding or null to record +8/+12, so that vector has one slot per mesh. CHR_CLUSTER.md 4"),
    0x6A1590: ("ClassCharacterInstance__releaseClothBindings", "for each non null record +12 slot: ClothInstancing_QueueDeferredRelease with the binding as this, then free the binding. From FFX_Chr_ReleaseInstance and ClassCharacter__unload. CHR_CLUSTER.md 7"),
    0x6A15E0: ("ClassCharacterInstance__releaseMeshInstances", "FFX_Gfx_ReleaseMeshInstance on every record +4 entry, always naming the gfx+0x11E4 cluster even for an instance made in gfx+0x1234. CHR_CLUSTER.md 7"),
    0x6A2B90: ("ClassCharacter__removeInstance", "removes the record from the ClassCharacter's +8..+16 vector under the mutex hHandle, the opposite of ClassCharacter__addInstance 0x6A0D00. CHR_CLUSTER.md 7"),
    0x668680: ("FFX_Gfx_ReleaseChrMeshRecords", "finds the CHR in the gfx+0xF9D4 map FFX_Gfx_CreateMeshInstance fills, releases each 12 byte {PMeshInstance, PWorldMatrix, PMeshInstanceBounds} record from both gfx+0x11E4 and gfx+0x1234, then erases the node. CHR_CLUSTER.md 7"),
    0x65A4C0: ("FFX_Gfx_ReleaseMeshInstance", "(instance, cluster): when the cluster is gfx+0x12D4 and the instance sits in the context's 3000 entry PMeshInstance pool it only frees the pool slot, anything else releases its world matrix (+4), its bounds (+28) and the instance. CHR_CLUSTER.md 7"),
    # cloth
    0x6E0800: ("ClothInstancingBinding__ctor", "the 20 byte binding of ClassCharacterInstance__addClothBinding: +0 ClothInstancingDynamicMesh, +4 the mesh instance's original m_dynamicMeshInstance, +8 its mesh, +16 the ClothInstancingDynamicMeshInstance. Zeroes +0, +4, +8, +16 only. CHR_CLUSTER.md 4"),
    0x6E0860: ("ClothInstancingDynamicMeshInstance__ctor", "PDynamicMeshInstance base, vtable 0xB4AB28, 128 segment slots, the dirty and staging tables zeroed and two 128 entry tables set to 1. 0x2010 bytes. CHR_CLUSTER.md 4"),
    0x6E17F0: ("ClothInstancingDynamicMesh__buildStreamDescs", "this = ClothInstancingDynamicMesh, arg = the mesh instance. One 16 byte segment descriptor per PMeshSegment (copied from the inner dynamic mesh when there is one), and for each cloth segment one 12 byte stream descriptor per vertex stream whose render data type is the Color semantic, type 3 (float4). Not the dirty flag setter. CHR_CLUSTER.md 4"),
    0x6E2E40: ("ClothInstancing_QueueDeferredRelease", "this = the cloth binding. Queues {binding+0 mesh, binding+16 instance, binding+8 inner mesh, 4} in g_ffxClothDeferredReleaseList, reusing a slot whose countdown went negative, so the cloth objects outlive the frames the render thread may still hold. CHR_CLUSTER.md 7"),
    # character lighting
    0x6A63F0: ("FFX_ChrLight_ctor", "0x234 byte character light manager: three PClusters (+0, +80, +160), maps of PMapPair, the 4 light set flag +0x1CC = 0, a semaphore at +392. CHR_CLUSTER.md 5"),
    0x6A6CD0: ("FFX_ChrLight_CreateSingleton", "allocates g_ffxChrLight once, from FFX_GraphicInitialize. CHR_CLUSTER.md 5"),
    0x6A6DD0: ("FFX_ChrLight_DestroySingleton", "tears g_ffxChrLight down, from FFX__graphicFinalize. CHR_CLUSTER.md 5"),
    0x6A6E30: ("FFX_ChrLight_Get", "returns g_ffxChrLight. CHR_CLUSTER.md 5"),
    0x6A96B0: ("FFX_ChrLight_RegisterMeshInstance", "only when +0x1CC is set (FFX_ChrLight_Initialize found exactly 4 lights): a new PMaterialSet with a copy of every material (in the manager's +160 cluster) and its parameter buffer, CharacterAmbientLight bound for effects with USE_CHARACTER_LIGHTING, then instance.m_materialSet = it (mesh+48 if none), LgtMatClr and LgtMatVec zeroed, and a {instance, 0.0, 2} row in the 100 entry table of dword_CCCA88 +248. CHR_CLUSTER.md 5"),
    0x6A8210: ("FFX_ChrLight_UnregisterMeshInstance", "undoes FFX_ChrLight_RegisterMeshInstance: when the +344 map knows the instance's set it restores m_materialSet from it (mesh+48 when that is null) and releases the cloned materials and set. Always clears the dword_CCCA88 table row. CHR_CLUSTER.md 7"),
    0x6AABF0: ("FFX_ChrLight_WriteLightMatParams", "for each material of instance.m_materialSet writes the LgtMatClr and LgtMatVec parameters from its two sources when the light flag +0x1CC is set. CHR_CLUSTER.md 5"),
    # what createInstance does after the meshes
    0x6B02E0: ("TextureAnimationManager__get", "returns the TextureAnimationManager singleton dword_CCCAA8. CHR_CLUSTER.md 6"),
    0x6E4100: ("FFX_Chr_ApplyLevelDepthBias", "(name, count, mesh instances), from ClassCharacter__createInstance. Level hash 393: every mesh instance's bounds (+28) gets a row {bounds, 1.0, 800.0}. Level hash 632 and name m121, or any other level and name m121 or m281: fixed biases on mesh instances 0..3. Frees the array it was given. CHR_CLUSTER.md 6"),
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
    0xCCCA84: ("g_ffxChrLight", "the character light manager, FFX_ChrLight_ctor 0x6A63F0. Byte +0x1CC gates the per instance material clone of FFX_ChrLight_RegisterMeshInstance. CHR_CLUSTER.md 5"),
    0xCDEFA4: ("g_ffxClothDeferredReleaseCount", "count of g_ffxClothDeferredReleaseList, 16 byte rows. CHR_CLUSTER.md 7"),
    0xCDEFA8: ("g_ffxClothDeferredReleaseList", "rows {cloth mesh, cloth instance, inner mesh, countdown} queued by ClothInstancing_QueueDeferredRelease. CHR_CLUSTER.md 7"),
    0xCCB454: ("g_ffxClothClonePool", "the pool FFX_Chr_CloneClassCharacterVertexData takes its 560 byte clone records, per mesh tables and cloth objects from. CHR_CLUSTER.md 8"),
    0xCCB250: ("g_ffxClonedChrCount", "count of g_ffxClonedChrList, the CHR+0x834 cloth clones the present thread steps. CHR_CLUSTER.md 8"),
    0x1942998: ("g_ffxDepthBiasCount", "rows in g_ffxDepthBiasTable, at most 256. CHR_CLUSTER.md 6"),
    0x194299C: ("g_ffxDepthBiasTable", "12 byte rows {bounds, float, float} that FFX_Chr_ApplyLevelDepthBias adds. CHR_CLUSTER.md 6"),
    0xCE5176: ("g_ffxDepthBiasActive", "set to 1 by FFX_Chr_ApplyLevelDepthBias whenever it adds a row. CHR_CLUSTER.md 6"),
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
