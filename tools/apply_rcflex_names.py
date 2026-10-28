"""
Apply the rcFlex and rcbgPSim names from reversing/RCFLEX.md to the FFX.exe IDB.

rcFlex is the PS2 mass-spring library (rcfxParticle, rcfxSpring, rcfxNail, rcfxDamp, rcfxGravity,
rcfxWind at 0x9285A0..0x92990B) and rcbgPSim (0x91F650..0x91FFC9) is its only client. The exact name
rcbgPSimAddGravity at 0x91F7C0 survives, the rest follow the alphabetical layout law (LUA_MODULE.md)
and the decompiled behaviour. Also here: the ppp operator glue that builds a PSim, the battle special
block and rcBg getters that feed its anchors, and the MapFx group pair whose sense was backwards.

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

    python apply_rcflex_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # rcfxParticle.c, the particle system (0x40 bytes) and its integrator
    0x9285A0: ("rcfxParticle_VecAdd", "rcfxParticle.c vector helper, a = b + c on vec4"),
    0x928600: ("rcfxParticle_VecScale", "rcfxParticle.c vector helper, a = b * s on vec4"),
    0x928660: ("rcfxParticleAddForce", "inserts a force into ps+56, kept sorted by the force's priority at +24. RCFLEX.md 3.1"),
    0x928710: ("rcfxParticleCalc", "one tick: zero forces, call every force's Apply (+20), then vel += f*(dt/mass), pos += vel*dt. Semi-implicit Euler"),
    0x928800: ("rcfxParticleClearForces", "calls each force's Delete (+4) and frees the force array"),
    0x928850: ("rcfxParticleDelete", "Final then free"),
    0x928870: ("rcfxParticleFinal", "frees the four arrays at +36..+48"),
    0x9288E0: ("rcfxParticleInit", "dt +8 = 0.03, +24 = 0.001, mass +32 = 0.1"),
    0x928950: ("rcfxParticleNew", "allocates and inits a 0x40 byte particle system"),
    0x928970: ("rcfxParticleReset", "calls each force's Reset (+16), then ResetVelocity"),
    0x9289B0: ("rcfxParticleResetVelocity", "zeroes the velocity array +44"),
    0x9289F0: ("rcfxParticleSetNum", "count +28, allocates pos +36, +40, vel +44, force +48 at 16*n each"),
    0x928A60: ("rcfxParticleUpdate", "target = t, while time < target: Calc, time += dt. The time base is whatever the caller passes, rcbgPSimProcess adds 0.5 per call"),
    # rcfxSpring.c
    0x928AA0: ("rcfxSpringApply", "per pair: d = pj-pi, len = max(|d|, rest/1000), F = d/len*(len-rest)*k + (vj-vi)*c, fi += F, fj -= F"),
    0x928C60: ("rcfxSpringDelete", "Final then free"),
    0x928C80: ("rcfxSpringFinal", "frees pairs +32 and rest lengths +36"),
    0x928CE0: ("rcfxSpringInit", "type 0x30005, k +40 = 0.8, damping +44 = 0.4"),
    0x928D50: ("rcfxSpringNew", "allocates and inits a 0x30 byte spring force"),
    0x928D70: ("rcfxSpringReset", "empty, the spring force's Reset slot"),
    0x928D80: ("rcfxSpringSetNum", "pairs 8*n at +32, rest lengths 4*n at +36"),
    # rcfxNail.c
    0x928DF0: ("rcfxNail_VecAdd", "rcfxNail.c vector helper"),
    0x928E50: ("rcfxNail_VecScale", "rcfxNail.c vector helper"),
    0x928EB0: ("rcfxNail_VecCopy", "rcfxNail.c vector helper"),
    0x928EE0: ("rcfxNail_VecSub", "rcfxNail.c vector helper"),
    0x928F40: ("rcfxNailApply", "target = anchor matrix (+56) * local (+36). Each pinned particle moves from the previous target toward it by (t-lastT)/(target-lastT), and its force is set to 0"),
    0x929070: ("rcfxNailDelete", "Final then free"),
    0x929090: ("rcfxNailFinal", "frees the nail arrays"),
    0x929100: ("rcfxNailInit", "type 0x30004, priority +24 = 2"),
    0x929170: ("rcfxNailNew", "allocates and inits a 0x40 byte nail force"),
    0x929190: ("rcfxNailReset", "the nail force's Reset slot: reloads the local points from the pinned particles, current target = anchor matrix * local (or local), previous = current, last time = ps time"),
    0x929230: ("rcfxNailSetNum", "count +28 and the index, local, current and previous arrays"),
    # rcfxDamp.c
    0x9292A0: ("rcfxDamp_VecScale", "rcfxDamp.c vector helper"),
    0x929300: ("rcfxDamp_VecSub", "rcfxDamp.c vector helper"),
    0x929360: ("rcfxDampApply", "f -= vel * c (+28)"),
    0x9293D0: ("rcfxDampInit", "type 0x30003, c +28 = 1e-4"),
    0x929420: ("rcfxDampNew", "allocates and inits a 0x20 byte damp force"),
    # rcfxGravity.c
    0x929440: ("rcfxGravity_VecAdd", "rcfxGravity.c vector helper"),
    0x9294A0: ("rcfxGravity_VecScale", "rcfxGravity.c vector helper"),
    0x929500: ("rcfxGravityApply", "f += g * mass"),
    0x929570: ("rcfxGravityInit", "type 0x30002"),
    0x9295C0: ("rcfxGravityNew", "allocates and inits a 0x2C byte gravity force"),
    # rcfxWind.c
    0x9295E0: ("rcfxWind_VecAdd", "rcfxWind.c vector helper"),
    0x929640: ("rcfxWind_VecScale", "rcfxWind.c vector helper"),
    0x9296A0: ("rcfxWind_VecCopy", "rcfxWind.c vector helper"),
    0x9296D0: ("rcfxWindApply", "gusts: when t > next, next += period and amp = 1 - rand()/32767*2 (CRT rand at 0x92971C, step thread). Smoothstep between amps, w = base + gust*amp, f += w or its projection on ps+40. No shipped content enables wind. RCFLEX.md 3.2 and 5"),
    0x929850: ("rcfxWindInit", "type 0x30006, period +64 = 1.0"),
    0x9298C0: ("rcfxWindNew", "allocates and inits a 0x60 byte wind force"),
    0x9298E0: ("rcfxWindSet", "base +28, gust +44, period +64"),
    # rcbgPSim.c, the only rcFlex client
    0x91F650: ("rcbgPSim_VecAdd", "rcbgPSim.c vector helper"),
    0x91F6B0: ("rcbgPSim_VecScale", "rcbgPSim.c vector helper"),
    0x91F710: ("rcbgPSim_VecCopy", "rcbgPSim.c vector helper"),
    0x91F740: ("rcbgPSim_VecSub", "rcbgPSim.c vector helper"),
    0x91F7A0: ("rcbgPSimAddDamp", "rcfxDampNew with the coefficient, AddForce"),
    0x91F850: ("rcbgPSimAddNail", "a nail force on a vertex list, anchored to matrix +28 or +92"),
    0x91F980: ("rcbgPSimAddWind", "rcfxWindNew, rcfxWindSet(base, gust, period), AddForce"),
    0x91F9B0: ("rcbgPSimClearModelData", "model +20 = 0"),
    0x91F9D0: ("rcbgPSimCreate", "one particle per model vertex, one spring per unique triangle or quad edge with rest = its length, then Reset"),
    0x91FD00: ("rcbgPSimCreateModel", "new rcbgModel (0x20001), sets its data and scale, stores it at +20"),
    0x91FD40: ("rcbgPSimDelete", "Final then free"),
    0x91FD60: ("rcbgPSimDraw", "the object's draw slot, forwards to the model"),
    0x91FD80: ("rcbgPSimFinal", "frees the particle system and model"),
    0x91FDC0: ("rcbgPSimGetModelPacket", "returns model +20 +32, the draw packet"),
    0x91FDD0: ("rcbgPSimGetNailPos", "copies an anchor translation out. Only the uncalled rcui debug code at 0x930DB0 and 0x930E70 uses it"),
    0x91FE00: ("rcbgPSimInit", "type 0x20004, slots Delete, Process, Draw"),
    0x91FE40: ("rcbgPSimNew", "allocates and inits a 0xB0 byte PSim"),
    0x91FE70: ("rcbgPSimProcess", "if +16 bit 0: rcfxParticleUpdate(ps, clock +168), clock += 0.5 (about 16 ticks of 0.03). Always writes positions back as s16 vertices (sub_9281D0). Step thread, from pppDrawMdlPSim_Draw. RCFLEX.md 3.3"),
    0x91FEF0: ("rcbgPSimSetNailNum", "+164 = anchors still to set"),
    0x91FF10: ("rcbgPSimSetNailPos", "anchor 0 or 1 translation (+76 / +140), set flag, count +164 down, Start at zero"),
    0x91FFB0: ("rcbgPSimStart", "Reset and +16 |= 1, the sim runs from now"),
    0x9281D0: ("rcbgModel_WriteParticleVerts", "writes particle positions as scaled s16 vertices into the ppp model blob (blob + blob[8]). Original name unknown, rcbgModel.c by position"),
    # ppp operator glue
    0x73D4D0: ("pppDrawMdlPSim_Create", "first Draw: builds the PSim from the op's model, nails (+34, max 4, vertex lists +122..), damp (+35), gravity (+40), wind (+64), draw flags +116..+120. RCFLEX.md 3.4"),
    0x73D7C0: ("pppDrawMdlPSim_GetScale", "scale from the length of the particle matrix row"),
    # who drives the anchors
    0x79D430: ("FFX_BtlSpecial_Step", "the battle special block, called every FFX_BtlEv_StepBattleContext (0x797325) and switching on the mode byte word_112A8E6. Mode 1 (slik02_00) pins the slik02 PSim to unit 0x17 bone 14 and a ship point. RCFLEX.md 6.3"),
    0x79DE60: ("FFX_BtlSpecial_SetBlock", "from btl:86 SetSpecialBlock and FFX_Btl_Init. Mode 1, 2 or 3 allocates a 0x870 state block (dword_11360A4) and stores the mode in word_112A8E6, anything else stores 0"),
    0x919F00: ("FFX_RcBg_GetPSim", "thunk, FFX_RcBg_GetPSimByIndex(i)"),
    0x9199F0: ("FFX_RcBg_GetPSimByIndex", "FFX_RcBgFP_GetGroupOpWork(91 + i, 6, 2), the PSim of map effect group 91 op 6"),
    0x919F10: ("FFX_RcBg_SetPSimNailPos", "rcbgPSimSetNailPos for anchors 0 and 1 only, null safe"),
    0x923100: ("FFX_RcBgFP_GetGroupOpWork", "finds a placed effect of a group (MapFx__f7292F0) and returns work slot a3 of op a2, +160 into the particle"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x9293C0: ("rcfxDampDelete", "was j_j_j_FFX_MemFreeLocked_2_3. The damp force's Delete slot, it has nothing to finalise"),
    0x929560: ("rcfxGravityDelete", "was j_j_j_FFX_MemFreeLocked_2_4. The gravity force's Delete slot"),
    0x929840: ("rcfxWindDelete", "was j_j_j_FFX_MemFreeLocked_2_5. The wind force's Delete slot"),
    0x729570: ("FFX_MapFx_SuspendGroup", "was FFX_MapFx_EnableGroup. Sets runtime flag 0x10, and FFX_MapFx_UpdatePlacedEffects skips a flagged record and unlinks its meshes (0x729B65, MapFx__f728E40), so this stops the group. RCFLEX.md 7"),
    0x7295B0: ("FFX_MapFx_ResumeGroup", "was FFX_MapFx_DisableGroup. Clears runtime flag 0x10, the group runs again. RCFLEX.md 7"),
    0x91C740: ("FFX_AtelSys_MapFx_013_SetGroupActive_resi", "was FFX_AtelSys_MapFx_013_SetGroupHidden_resi. (group, active): active != 0 calls FFX_MapFx_ResumeGroup, else FFX_MapFx_SuspendGroup. The old name had the sense backwards. RCFLEX.md 7"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
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
