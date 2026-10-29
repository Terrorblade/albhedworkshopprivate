#!/usr/bin/env python3
"""
datamap.py - the FFX.exe .data allocation map: every named global, its size, its owning subsystem,
who writes it and from which thread, and every run of bytes between named globals.

Two steps. The dump reads a COPY of the IDB through idalib and writes one JSON file. The report
reads that JSON with plain Python, so it can be rerun and changed without IDA.

  python -I datamap.py dump --idb path/to/COPY/FFX.exe.i64 --out dump.json
  python -I datamap.py report --dump dump.json [--table ../reversing/DATA_MAP_TABLE.md]
                                               [--runs N] [--min-gap 0x10000]

dump has to run under the IDA venv python ("<IDA>/venv/Scripts/python.exe"). It never saves the
database. report runs under any Python 3.

What counts as a reference. FFX.exe ships a full .reloc, and every address the linker emitted is
in it. So the dump takes the relocation table (IDA's fixups, which match the PE .reloc entry for
entry, 283,644 HIGHLOW relocations of which 133,148 land in .data) as the census of references.
An operand IDA turned into a .data address WITHOUT a relocation is not a reference to that
storage. It is a number that happens to fall in the range: a battle id compared against
FFX_Btl_GetBattleId, a GS register value, or a PS2 main RAM address the port kept as a literal.
Those are listed separately as literals, see reversing/DATA_MAP.md section 4.

How a run is classified. Every relocated target, every user named address and every defined
item that some relocation reaches is a point. A point owns the bytes from itself to the next
point. Its own size is the IDB type size, or the access width for a scalar. The rest of the span
is its tail. A tail is explained when the point is used as a base (an indexed operand, an lea, or
its address taken), or when a curated region in REGIONS covers it. Otherwise it is unexplained.
Named means the byte is inside a user named point's own size, inside its explained tail, or
inside a curated region that has a name.

Tags follow the reversing docs: [V] read in the binary at the address given, [M] measured by a
scan (this script is the scan), [I] inferred.
"""

import bisect
import json
import re
import sys

DATA_START = 0xC0A000
DATA_END = 0x25D7000

# ---------------------------------------------------------------------------------------------
# Curated regions. Each one was read by hand, see reversing/DATA_MAP.md section 3 for the
# evidence in full. (start, end, name, subsystem, tag, evidence, what). name None means the
# region is explained but nothing is named after it. A region inside another one refines it.
# ---------------------------------------------------------------------------------------------

REGIONS = [
    (0xCEC258, 0xD2C258, "g_ffxMapFxPppHeapArena", "Map effects", "V",
     "FFX_MapFx_LoadParticleSet push 40000h 0x72A814 with push offset 0x72A819",
     "the map particle set's ppp heap, 256 KB, handed to FFX_Ppp_InitHeapForSet"),
    (0xD32860, 0xD4B460, "g_ffxPppPartSlots", "Magic and effects", "V",
     "MapFx__f72AC30 memset(0xD32860, 0, 18C00h) at 0x72AC30, PppCore__f72C1B0 indexes 396 * slot",
     "256 ppp part slots of 396 bytes, the PS2 partmast part table"),
    (0xD524A8, 0xE524A8, "g_ffxPmcomRecvArena", "Debug and dev", "V",
     "sub_760610 0x760621 resets the bump cursor dword_D524A0 to it, sub_760080 bumps it",
     "PMCOM (PS2 particle master protocol) receive arena, 1 MB, dev tool link only"),
    (0xE524F0, 0xF524F0, "g_ffxPmcomRing", "Debug and dev", "V",
     "sub_762F50 wraps dword_D524A4 at byte_F524F0 (0x762F6C), sub_761E20 reads a 512x416 frame into its start",
     "PMCOM ring, 1 MB. Its first 0xD0000 also takes the 512x416 screen grab"),
    (0xF224F0, 0xF524F0, None, "Debug and dev", "V",
     "sub_761E20 converts the grab at 0xE524F0 into RGB here and sends it in 4 KB IM packets",
     "the RGB copy of the PMCOM screen grab, inside the ring"),
    (0xF524F0, 0xFD24F0, "g_ffxPmcomDoubleBuffer", "Debug and dev", "V",
     "sub_760080 0x7601CB: byte_F524F0 + (dword_D4B4C0 & 1) << 18",
     "PMCOM double buffer, two halves of 0x40000"),
    (0xFD24F0, 0x10D24F0, "g_ffxVfxPppHeapArena", "Magic and effects", "V",
     "sub_760610 0x760615 and sub_761D60 0x761DBA: FFX_Ppp_InitHeapForSet(dword_10D4CFC, 0, 0xFD24F0, 100000h)",
     "the PMCOM effect slots' ppp heap, 1 MB"),
    (0x10D5654, 0x10D7640, None, "Debug and dev", "I",
     "sub_76B210 and sub_76B3D0 memcpy the outgoing payload here",
     "PMCOM send staging"),
    (0x10D7640, 0x10E7640, None, "Debug and dev", "I",
     "sub_76B2D0 0x76B2EC stores it at driver+0x14, the span to the next reference is 0x10000",
     "PMCOM protocol driver buffer"),
    (0x10E7880, 0x1127C80, "g_ffxDevPrintfRing", "Debug and dev", "V",
     "sub_76B860 0x76B88D imul 404h, index wraps at 256",
     "256 vsprintf lines of 1028 bytes for the dev link"),
    (0x112CA90, 0x1133350, "g_ffxSaveData", "Save data and party", "V",
     "FFX_SaveFile_InstallBlock 0x8B54A0 memcpy(dest, image + 0x40, 0x68C0), SAVE_BLOCK_MAP.md tiles it",
     "the save block, 26,816 bytes. Hash visible, the IDB types only its first word"),
    (0x113FE10, 0x119FE10, "g_ffxResidentEffectHeapArena", "Magic and effects", "V",
     "sub_7FD760 push 60000h 0x7FD78F with push offset 0x7FD794",
     "resident effect unit 0 heap, 384 KB, set up once at boot"),
    (0x11A1260, 0x12A12F0, "g_ffxEffectPacketArenaA", "Magic and effects", "V",
     "sub_7EC890 0x7EC8A9 stores it as dword_2332E90, FFX_MagicResident_OvlEffectsPass 0x80069D adds 0F0000h for the limit",
     "per frame effect packet arena. The sphere grid board effect file is loaded at its start too (typed 0x5EDA0)"),
    (0x12A8DA8, 0x12E95D8, None, "Magic and effects", "I",
     "stored at prim+0x180 by sub_A6ACB0 0xA6B009, sub_A6B220, sub_A6C3D0, op_md0_onef, and at +0x10 by sub_A73D90",
     "a draw work buffer the md0 primitive builders point at, span to the next reference"),
    (0x1303D40, 0x1313D40, None, "Debug and dev", "I",
     "sub_84EDE0 0x84EDEA (KSEG1 alias +0x20000000)",
     "SG debug protocol buffer A"),
    (0x1313D40, 0x1323D40, None, "Debug and dev", "I",
     "sub_84EDE0 0x84EDFB",
     "SG debug protocol buffer B"),
    (0x1340870, 0x1840870, None, "Menu and UI", "V",
     "FFX_MenuSys_RunFrame push 500000h 0x8AB0D2, memset of the whole span",
     "the menu system arena, 5 MB, cleared when the menu system starts. Battle effect work reuses parts of it. It has no name of its own because its first sub-buffer already owns the address"),
    (0x1340870, 0x1410870, "g_ffxMenuSysGsBackupBuffer", "Menu and UI", "V",
     "sub_8AA690 reads 13 GS chunks into it",
     "menu GS backup, the first 0xD0000 of the menu arena"),
    (0x1410870, 0x14C0870, None, "Menu and UI", "I",
     "sub_8A9510 0x8A952E and sub_8AAC80, 13 chunk loops", "second GS save area"),
    (0x14C0870, 0x14E0870, None, "Menu and UI", "I", "getter sub_8BEB20 / sub_8BED90", "menu work buffer"),
    (0x14E0870, 0x1540870, None, "Menu and UI", "I", "getter sub_8BEB50", "menu work buffer"),
    (0x1540870, 0x1574F70, None, "Magic and effects", "I", "FFX_Oef2_Op132 pushes it twice", "Oef2 op 132 work buffer inside the menu arena"),
    (0x1574F70, 0x1576F70, None, "ATEL and events", "I",
     "FFX_Menu_SetupAtelContext passes it to FFX_Atel_SetupStaticContext(6, ...)", "menu ATEL context work"),
    (0x157F130, 0x1586F70, None, "Menu and UI", "I", "TOSwapInternationalEnvExecMenu loads group 13 into it", "international env menu file"),
    (0x1586F70, 0x158EF70, None, "Menu and UI", "I", "FFX_MenuSys_RunFrame sa_task__f77A1D0(2, it, 0)", "sa_menu task work"),
    (0x15A0870, 0x15C0870, None, "Menu and UI", "I", "FFX_Module0_ExecBoot sub_88D190(42, 0, it)", "boot module file buffer"),
    (0x15C0870, 0x1640870, "g_ffxMenuGsPacketBuffer", "Menu and UI", "V", "sub_8BEF30 push 80000h 0x8BEF35, sub_9047C0(it, 80000h, 480000)", "menu GS packet buffer, 512 KB"),
    (0x1640870, 0x16931A0, None, "Magic and effects", "I", "FFX_Oef2_ExplodeModel, getters sub_8A9BA0 / sub_8BEA30", "explode fragment work inside the menu arena"),
    (0x16931A0, 0x16AB1A0, None, "Menu and UI", "I", "menu work +71272 (FFX_Menu_Build, FFX_SphereGrid_StepNodeKindChange)", "sphere grid node kind work"),
    (0x16AB1A0, 0x16AD8B0, "g_ffxMenuPageRecords", "Menu and UI", "V", "sub_A572E0 0xA572F0 rep stosd 9C4h dwords, then g_ffxMenuPages = it", "menu page records, 0x2710"),
    (0x16AD8B0, 0x16C1870, "g_ffxMenuWorkBlock", "Menu and UI", "V", "sub_A572E0 0xA57333 rep stosd 4BF0h dwords, then g_ffxMenuWork = it", "menu work block, 0x12FC0"),
    (0x16C1870, 0x173E870, "g_ffxSphereGridPppHeapArena", "Menu and UI", "V", "FFX_SphereGrid_StepWarp push 7D000h 0xA48079", "sphere grid ppp heap, 0x7D000. The IDB types 0x6F000 and the heap runs over g_ffxBtlMixPrepareTable 0x1730870, which only battle uses"),
    (0x1740870, 0x1760870, None, "Menu and UI", "I", "FFX_SphereGrid_LoadLayout loads the board layout file into it", "sphere grid layout file"),
    (0x1760870, 0x17C0870, "g_ffxMenuEffectPacketBuffer", "Menu and UI", "I", "sub_A545A0 and FFX_Menu_DrawAllPartsIgnoringCatchUp set g_ffxVfxPacketCursor to it", "menu effect packet buffer"),
    (0x17C0870, 0x1840870, None, "Magic and effects", "I", "sub_7F73D0 stores it at +4 of an effect mesh record", "effect mesh work, the last 512 KB of the menu arena"),
    (0x1876E00, 0x19311E8, "g_ffxMapWork", "Map and field", "V",
     "sub_90CD90 0x90CDC8 writes [esi+0BA3E0h] with esi = &g_ffxMapWork, the largest field offset seen",
     "the map work struct, about 745 KB: 72 + 72 records of 0x460 (0x908F50, 0x908940), the map out header g_ffxMapOut at +0x9CAA8"),
    (0x1945140, 0x1984C80, None, "Phyre engine", "I",
     "PDeferredLighting*, PDepthOfField*, PMotionBlur*, PMeshParticleSystemBase ClassDescriptor_ctor push these",
     "Phyre class descriptor member storage, three arrays per class"),
    (0x1984C80, 0x1A84C80, "g_ffxFmvDiscardBlockBuffer", "Movies", "V",
     "PVideoPlaybackWin32_readAllBlocks 0xA277A0, MOVIE_DECODE.md",
     "1 MiB scratch the movie reader throws blocks into"),
    (0x1A8610C, 0x2286140, "g_ffxPs2AddressWindow", "PS2 address window", "M",
     "no relocation in the whole span (the PE .reloc and the IDB fixups agree), the literals in section 4 land in it",
     "8 MB that nothing references by symbol. Live code writes PS2 main RAM addresses 0x1C80000..0x2000000 into it as literals"),
    (0x2286170, 0x2288170, None, "Menu and UI", "I", "sub_A5DDA0 0xA5DDF9 stores it as dword_2286140", "menu log head"),
    (0x2288170, 0x22C8270, None, "Menu and UI", "V",
     "nofunc 0xA5E350: ((dword_2286168 - n) & 1Fh) << 0Dh indexes 17 fields at 0x2288230..0x228A230",
     "32 records of 0x2000 read back by the menu log printer"),
    (0x22DB170, 0x22FB180, "g_ffxResidentEffectObjects", "Magic and effects", "I",
     "unit 0 object pointer at 0xC88AA4, FFX_Effect_InitUnit(.., 0x200, ..) from sub_7FD760",
     "512 effect objects of resident unit 0 (YONISHI_ENGINE.md 7.3)"),
    (0x23227E4, 0x2332860, None, "Debug and dev", "M",
     "no relocation and no literal lands in it. byte_23227E3 just before is the SG debug 'effect target memory save' flag",
     "dead: 64 KB nothing in the binary reaches"),
    (0x2333500, 0x2343520, "g_ffxOef2Op078FallbackBuf", "Magic and effects", "I",
     "FFX_Oef2_Op078 0x80F2AE uses it when sub_7B7EB0 returns null", "Oef2 op 78 fallback buffer"),
    (0x2343520, 0x23C3520, "g_ffxResidentPppHeapArena", "Magic and effects", "V",
     "FFX_MagicResident_OvlInit push 80000h 0x8005C6 with push offset 0x8005CB",
     "resident magic ppp heap, 512 KB"),
    (0x23CC480, 0x2400480, "g_ffxMenuWorldMapImage", "Menu and UI", "I",
     "FFX_Menu_ExecModule23WorldMap 0x8E299C loads menu group 23 into it and uploads 512x416 PSMT8, the file is 212,992 bytes",
     "world map image load buffer, sized to the file"),
    (0x2400480, 0x25CC580, None, "Unreferenced tail", "M",
     "no relocation lands in it and the only literals that do are constants, not addresses",
     "1.8 MB past the world map image that nothing in the binary reaches"),
]

# ---------------------------------------------------------------------------------------------
# Subsystem rules, first match wins. They run on a name with maybe_, g_ffx, g_phyre, FFX_ and
# FFX__ stripped, so g_ffxBtlFoo and FFX_Btl_Foo both see "Btl...".
# ---------------------------------------------------------------------------------------------

SUBSYSTEM_RULES = [
    (r"^\?\?_R|^\?\?_7|ClassDescriptor|ClassDesc|^Phyre|^P[A-Z][a-z]|^g_phyre|^(Vector[234]|Point3|Quat|Matrix[34]|PLOD)", "Phyre engine"),
    (r"^(lua|luaL|luaK|luaV|luaD|luaH|luaO|luaS|luaT|luaU|luaX|luaY|luaZ|luaG|luaF|luaC|luaE|luaI|lparser|llex)", "Lua"),
    (r"^(mkvparser|Mkv|vpx|vp8|vp9|vorbis|Ogg|ogg|PVideo|PhyFMV|movie|Fmv|FmvMgr|Premovie|Movie|SimpleBlock|BlockGroup|EOSBlock|VideoTrack|SqsStraight)", "Movies"),
    (r"^(Fmod|FMOD|\?\w+@FMOD|FfxFmod|FmodCmd|Snd|SndIop|Se[A-Z_]|Bgm|Voice|SpuStream|Sound|Sfx|Footstep|Shout)", "Audio"),
    (r"^(_|\?|std|Concurrency|vc|crt|zlib|inflate|deflate|adler|crc32|Bullet|bt[A-Z]|DebugBreak|start$)", "CRT and libraries"),
    (r"^(Btl|Battle|BtlMenu|BtlOd|BtlResult|BtlEv|BtlAi|BtlSpecial|BtlExplode|Spoils|Overdrive|Yojimbo|Monster|Enc|Encounter|Arena|bt_|Mix|Ctb|Command)", "Battle"),
    (r"^(Oef2|Opp|opp|opu|op_|ppp|Ppp|PppCore|Vfx|VfxPm|VFX|Magic|MagicFile|MagicResident|MagicTex|MagicDraw|Effect|ResidentEffect|Glue|rcfx|Yonishi|Cloth|Distortion|Radial|Broken|Shadow|Magic706|u_|Explode|Blur|DreamFilter)", "Magic and effects"),
    (r"^(MapFx|MapParticle)", "Map effects"),
    (r"^(Menu|MenuSys|MenuLayout|MenuScene|MenuObj|MenuScript|MenuRes|Module|SaMenu|sa_|sa[A-Z]|SphereGrid|NameInput|EscMenu|FFEscMenu|SaveLoadMenu|MesWin|Sprite|Title|Toast|Config|SaveUi|TO[A-Z]|GuideMap|Kernel|ScreenFade)", "Menu and UI"),
    (r"^(Font|Text|KernelString|Mes|Glyph|Lang)", "Text and fonts"),
    (r"^(Atel|AtelSys|Ev|EvDev|Event|Cutscene|Scenario|Story|Script)", "ATEL and events"),
    (r"^(Save|SaveData|SaveFile|SaveMgr|Autosave|InitNewSaveData|Playtime|Gil|Item|Equip|Party|Char[A-Z]|Name|Treasure|AlBhed|Celestial|Stats|Sphere)", "Save data and party"),
    (r"^(Came|Camera|camera|PlayerCam|SetCamera)", "Camera"),
    (r"^(Map|Field|rcbg|RcBg|rcui|Walkmesh|Level|Water|Bg|Placement|Interaction|Stroll|Player|Target|Chocobo|Guild)", "Map and field"),
    (r"^(Ch[A-Z_]|Chr|ChrLight|ChCache|Mot|Motion|Tidus|Cloned|Character|ClassCharacter|Dyn|Weapon|LoadCharacterData)", "Characters and motion"),
    (r"^(Input|Pad|VirtualPad|Key|Button|Mouse)", "Input"),
    (r"^(Main|StepPacing|Step|Pacing|VsyncEmu|Vsync|Frame|Time|Timing|Timer|Game[A-Z]|Global|Sync|SyncData|Application|FFXApplication|Boot|BootSequence|Quit|First|Seconds|Precise|Fixed|InitSubsystems|ParseCommandLine)", "Main loop and timing"),
    (r"^(Gs|Gfx|GfxCtx|Tex|EvTex|SysTex|Dma|DynDrawList|Host|Ps2|Depth|Hdao|Anisotropy|Render|Window|CWindow|Borderless|Back|Scene|Draw|F117|graphic|Graphic|PostFx|PostProcess|ClassDynamicGeometry|VFXDynamicGeometry|Vu0)", "Render and PS2 emulation"),
    (r"^(Debug|DebugPage|Dbg|Sg|SG|sg[A-Z]|SgDebug|SgCapture|RomDev|RcDev|libmscd|Yanagi|yi|tklib|TkHarness|Auto|AutoTest|Prof|Not|PTTY|Viewer|Set[A-Z]\w*Enabled|Toggle)", "Debug and dev"),
    (r"^(Asset|Rom|Vbf|VbfManager|AsyncLoad|Async|DataFile|Path|Data|LoadDll|Mem|mem|Memory|Alloc|Free$|Scratch|Intl|Ps2Path|sce|syncStrCd|File|InitFileSystem|PoolSet)", "Assets, memory and files"),
    (r"^(Rand|Rng)", "RNG"),
    (r"^(Math|Vec3|Mtx|Spline)", "Math"),
    (r"^(Booster|Ach|Steam|Launcher|Copyright|In[A-Z]|Is[A-Z]|Current|Blitz|Minigame|Jecht|Rikku)", "PC port and features"),
]

# Threads. (tag, root function VA, label). STEP is the simulation, the rest are the census in
# reversing/THREADS_AND_CLOCKS.md part 1.
THREAD_ROOTS = [
    ("S", 0x820AE0, "step (FFX_MainStep)"),
    ("I", 0x820860, "init (FFX_MainInit, runs on a JobWorker at boot)"),
    ("I", 0x6416C0, "init (FFX_BootSequence)"),
    ("W", 0x62EE50, "main thread outside the step (WinMain, frameTick, render submit)"),
    ("R", 0x616010, "PhyreEngineRenderThread (Phyre_RenderThread_Main)"),
    ("J", 0x703A20, "JobWorker (JobWorker__vf01)"),
    ("A", 0x6FA1B0, "FmodManager (FmodManager__threadMain)"),
    ("P", 0x68D740, "AsyncControllerTaskManager (FFX_InputThread__main)"),
    ("L", 0x691360, "AsyncLoadManager (AsyncLoadManager__workerThread)"),
    ("V", 0x679C60, "VsyncEmulationThread (VsyncThreadImpl__run)"),
    ("T", 0x6BD1E0, "AutoTest stdin reader (AutoTestManager__stdinThread)"),
    ("M", 0xA27CF0, "movie reading thread (bodies reached from PVideoPlaybackWin32__startReadingThread)"),
    ("M", 0xA27F20, "movie decoding thread (bodies reached from __startVideoDecodingThread)"),
]


# =============================================================================================
# dump (idalib)
# =============================================================================================

def dump(idb, out):
    import idapro
    if idapro.open_database(idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % idb)
    try:
        _dump(out)
    finally:
        idapro.close_database(save=False)


def _dump(out):
    import idaapi
    import idautils
    import idc
    import ida_bytes
    import ida_fixup
    import ida_funcs
    import ida_name
    import ida_segment
    import ida_xref

    segs = []
    for s in idautils.Segments():
        seg = ida_segment.getseg(s)
        segs.append([ida_segment.get_segm_name(seg), seg.start_ea, seg.end_ea])
    # initialized .data ends where IDA stops having file bytes
    lo, hi = DATA_START, DATA_END
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if ida_bytes.is_loaded(mid):
            lo = mid
        else:
            hi = mid
    init_end = hi

    # every relocation into .data
    refs = []
    targets = set()
    ea = ida_fixup.get_first_fixup_ea()
    nfix = 0
    while ea != idaapi.BADADDR:
        nfix += 1
        v = ida_bytes.get_dword(ea)
        if DATA_START <= v < DATA_END:
            head = ida_bytes.get_item_head(ea)
            code = ida_bytes.is_code(ida_bytes.get_flags(head))
            f = ida_funcs.get_func(head)
            kind = "?"
            for x in idautils.XrefsFrom(head, 0):
                if x.to == v:
                    kind = {ida_xref.dr_O: "o", ida_xref.dr_W: "w", ida_xref.dr_R: "r"}.get(x.type, "?")
                    if kind in "wr":
                        break
            text = idc.GetDisasm(head) if code else ""
            text = text.split(";")[0].strip()
            refs.append([ea, v, head, 1 if code else 0, f.start_ea if f else -1, kind, text])
            targets.add(v)
        ea = ida_fixup.get_next_fixup_ea(ea)

    # every named or defined address in .data
    mask = ida_bytes.FF_REF | ida_bytes.FF_NAME | ida_bytes.FF_LABL

    def wanted(fl):
        return (fl & mask) != 0 or (ida_bytes.is_head(fl) and ida_bytes.is_data(fl))
    pts = set(targets)
    ea = ida_bytes.next_that(DATA_START - 1, DATA_END, wanted)
    while ea != idaapi.BADADDR and ea < DATA_END:
        pts.add(ea)
        ea = ida_bytes.next_that(ea, DATA_END, wanted)

    points = []
    for ea in sorted(pts):
        fl = ida_bytes.get_flags(ea)
        isdata = ida_bytes.is_data(fl) and ida_bytes.is_head(fl)
        kind = ("struct" if ida_bytes.is_struct(fl) else "str" if ida_bytes.is_strlit(fl) else
                "align" if ida_bytes.is_align(fl) else "data") if isdata else "undef"
        cmt = (idc.get_cmt(ea, 1) or idc.get_cmt(ea, 0) or "").replace("\n", " ")[:200]
        points.append([ea, ida_name.get_ea_name(ea) or "", 1 if ida_bytes.has_user_name(fl) else 0,
                       ida_bytes.get_item_size(ea) if isdata else 1, kind, idc.get_type(ea) or "", cmt])

    # code operands IDA resolved into .data that carry no relocation (literals)
    lits = []
    for p in points:
        if p[0] in targets:
            continue
        for x in idautils.XrefsTo(p[0], 0):
            if ida_bytes.is_code(ida_bytes.get_flags(x.frm)):
                f = ida_funcs.get_func(x.frm)
                lits.append([x.frm, p[0], f.start_ea if f else -1,
                             idc.GetDisasm(x.frm).split(";")[0].strip()])

    funcs = {}
    for fs in idautils.Functions():
        funcs[fs] = ida_funcs.get_func_name(fs)
    edges = []
    for fs in funcs:
        for x in idautils.XrefsTo(fs, 0):
            f = ida_funcs.get_func(x.frm)
            if not f or f.start_ea == fs:
                continue
            if x.type in (ida_xref.fl_CN, ida_xref.fl_CF, ida_xref.fl_JN, ida_xref.fl_JF):
                edges.append([f.start_ea, fs, "c"])
            elif x.type == ida_xref.dr_O:
                edges.append([f.start_ea, fs, "o"])

    json.dump({"segments": segs, "init_end": init_end, "fixups_total": nfix, "refs": refs,
               "points": points, "literals": lits,
               "funcs": {str(k): v for k, v in funcs.items()}, "edges": edges},
              open(out, "w"))
    print("relocations %d, into .data %d, points %d, literals %d, functions %d, edges %d"
          % (nfix, len(refs), len(points), len(lits), len(funcs), len(edges)))


# =============================================================================================
# report (plain Python)
# =============================================================================================

def _is_noise(name):
    # signature matches that landed on engine code, they say nothing about the owner
    return re.match(r"^(j_)*(__RTC_NumErrors|unknown_libname|__cfltcvt_init|nullsub)", name) is not None


def _strip(name):
    n = re.sub(r"^(j_)*(maybe_)?", "", name)
    if n.startswith("g_phyre"):
        return "Phyre" + n[7:]
    n = re.sub(r"^(g_ffx|g_|FFX__|FFX_)", "", n)
    return n


def subsystem_of_name(name):
    if not name:
        return None
    for pat, sub in SUBSYSTEM_RULES:
        if re.search(pat, name) or re.search(pat, _strip(name)):
            return sub
    return None


def _is_placeholder(name):
    return (not name or re.match(r"^(sub|nullsub|j_sub|loc|unk|dword|word|byte|qword|off|flt|dbl|stru)_[0-9A-F]+$", name) is not None)


class Model:
    def __init__(self, d):
        self.d = d
        self.init_end = d["init_end"]
        self.funcs = {int(k): v for k, v in d["funcs"].items()}
        self.fstarts = sorted(self.funcs)
        self.points = {p[0]: p for p in d["points"]}
        self.refs_to = {}
        for r in d["refs"]:
            self.refs_to.setdefault(r[1], []).append(r)
        # a point is real when a relocation reaches it or a user named it
        self.real = sorted(a for a, p in self.points.items() if a in self.refs_to or p[2])
        self._fsub = {}
        self._reach = None
        self.callers = {}
        for e in d["edges"]:
            if e[2] == "c":
                self.callers.setdefault(e[1], []).append(e[0])

    # -- subsystems -----------------------------------------------------------------------
    def _own_subsystem(self, fs):
        name = self.funcs.get(fs, "")
        if _is_placeholder(name) or _is_noise(name):
            return None
        return subsystem_of_name(name)

    def func_subsystem(self, fs, depth=0):
        if fs in self._fsub:
            return self._fsub[fs]
        sub = self._own_subsystem(fs)
        if sub is None and fs in self.funcs:
            # the layout law: a placeholder sits in its object file among named neighbours, so
            # take the vote of the nearest classified names within 16 KB either side
            i = bisect.bisect_left(self.fstarts, fs)
            votes = {}
            seen = 0
            for step in (-1, 1):
                j = i + step
                k = 0
                while 0 <= j < len(self.fstarts) and abs(self.fstarts[j] - fs) < 0x4000 and k < 4:
                    s2 = self._own_subsystem(self.fstarts[j])
                    if s2:
                        votes[s2] = votes.get(s2, 0) + 1
                        k += 1
                        seen += 1
                    j += step
            if votes:
                best, n = max(votes.items(), key=lambda kv: kv[1])
                if n * 10 >= seen * 6:
                    sub = best
        if sub is None and depth == 0 and fs in self.callers:
            # last resort, the callers' vote
            votes = {}
            for c in self.callers[fs][:64]:
                s2 = self.func_subsystem(c, 1)
                if s2:
                    votes[s2] = votes.get(s2, 0) + 1
            if votes:
                best, n = max(votes.items(), key=lambda kv: kv[1])
                if n * 10 >= sum(votes.values()) * 6:
                    sub = best
        if depth == 0:
            self._fsub[fs] = sub
        return sub

    def idb_name(self, a):
        p = self.points.get(a)
        return p[1] if p else ""

    def regions_over(self, a, b):
        return [r for r in REGIONS if r[0] < b and a < r[1]]

    def region_at(self, a):
        best = None
        for r in REGIONS:
            if r[0] <= a < r[1] and (best is None or r[1] - r[0] < best[1] - best[0]):
                best = r
        return best

    def outer_region_at(self, a):
        best = None
        for r in REGIONS:
            if r[0] <= a < r[1] and (best is None or r[1] - r[0] > best[1] - best[0]):
                best = r
        return best

    def subsystem(self, a):
        p = self.points.get(a)
        for r in REGIONS:
            if p and r[0] == a and r[2] == p[1]:
                return r[3], "region"
        if p and p[2]:
            s = subsystem_of_name(p[1])
            if s:
                return s, "name"
        r = self.region_at(a)
        if r:
            return r[3], "region"
        votes = {}
        for ref in self.refs_to.get(a, []):
            s = self.func_subsystem(ref[4]) if ref[4] != -1 else None
            if s:
                votes[s] = votes.get(s, 0) + 1
        if votes:
            return max(votes.items(), key=lambda kv: kv[1])[0], "readers"
        if a < self.init_end:
            return "Initialized tables", "none"
        return "Unattributed", "none"

    def owner_intervals(self):
        bounds = set(self.real) | {DATA_START, DATA_END}
        for r in REGIONS:
            bounds.add(r[0])
            bounds.add(r[1])
        bounds = sorted(b for b in bounds if DATA_START <= b <= DATA_END)
        out = []
        for x, y in zip(bounds, bounds[1:]):
            i = bisect.bisect_right(self.real, x) - 1
            p = self.real[i] if i >= 0 else None
            reg = self.region_at(x)
            sub = None
            if p is not None and self.points[p][2] and (reg is None or p > reg[0] or (p == reg[0] and self.points[p][1] != reg[2])):
                sub = subsystem_of_name(self.points[p][1])
            if sub is None and reg is not None:
                sub = reg[3]
            if sub is None:
                sub = self.subsystem(p)[0] if p is not None else "Unattributed"
            out.append((x, y, sub))
        return out

    # -- extents --------------------------------------------------------------------------
    def is_base(self, a):
        for r in self.refs_to.get(a, []):
            t = r[6]
            if r[5] == "o" or t.startswith("lea") or "[" in t and ("*" in t or re.search(r"\[e[a-ds][xip]\]?|\+e[a-ds][xip]", t)):
                return True
            if r[3] == 0:
                return True  # a data pointer to it, so it is the start of something
        return False

    def extents(self):
        """yield (addr, next, own, tail_explained)"""
        real = self.real
        for i, a in enumerate(real):
            nxt = real[i + 1] if i + 1 < len(real) else DATA_END
            p = self.points[a]
            own = min(max(p[3], 1), nxt - a)
            if p[4] == "undef":
                own = min(4, nxt - a)
            yield a, nxt, own, self.is_base(a)

    # -- threads --------------------------------------------------------------------------
    def reach(self):
        if self._reach is not None:
            return self._reach
        roots_by_tag = {}
        for tag, ea, _ in THREAD_ROOTS:
            roots_by_tag.setdefault(tag, []).append(ea)
        all_roots = {ea for _, ea, _ in THREAD_ROOTS}
        adj = {}
        for e in self.d["edges"]:
            if e[2] == "o" and e[1] in all_roots:
                continue
            adj.setdefault(e[0], []).append(e[1])
        reach = {}
        for tag, roots in roots_by_tag.items():
            seen = set()
            stack = list(roots)
            if tag == "M":
                stack = [t for r in roots for t in adj.get(r, [])]
            while stack:
                f = stack.pop()
                if f in seen:
                    continue
                seen.add(f)
                stack.extend(adj.get(f, []))
            reach[tag] = seen
        # main thread outside the step: WinMain's closure minus the step's
        reach["W"] = reach["W"] - reach["S"]
        self._reach = reach
        return reach

    def writer_threads(self, a):
        reach = self.reach()
        tags = set()
        writers = set()
        for r in self.refs_to.get(a, []):
            if r[5] != "w":
                continue
            writers.add(r[4])
            hit = False
            for tag in "SIWRJAPLVTM":
                if r[4] in reach.get(tag, ()):
                    tags.add(tag)
                    hit = True
            if not hit:
                tags.add("?")
        order = "SIWRJAPLVTM?"
        return "".join(t for t in order if t in tags) or "-", writers


def report(path, table_out=None, nruns=25, min_gap=0x10000):
    d = json.load(open(path))
    m = Model(d)
    total = DATA_END - DATA_START
    cls = bytearray(total)  # 0 unexplained, 1 explained, 2 named
    UNEX, EXPL, NAMED = 0, 1, 2

    lift = bytes([max(x, EXPL) for x in range(256)])

    def mark(a, b, c):
        a = max(a, DATA_START) - DATA_START
        b = min(b, DATA_END) - DATA_START
        if b > a:
            cls[a:b] = bytes([NAMED]) * (b - a) if c == NAMED else bytes(cls[a:b]).translate(lift)

    # padding: a tail of under 16 bytes before an aligned next point is alignment
    rows = []
    for a, nxt, own, base in m.extents():
        p = m.points[a]
        user = p[2] == 1
        mark(a, a + own, NAMED if user else EXPL)
        if base:
            mark(a + own, nxt, NAMED if user else EXPL)
        elif nxt - (a + own) < 16:
            mark(a + own, nxt, EXPL)
        rows.append((a, nxt, own, base))
    # unnamed regions explain, named regions name
    for r in REGIONS:
        mark(r[0], r[1], EXPL)
    for r in REGIONS:
        if r[2] and m.idb_name(r[0]) == r[2]:
            mark(r[0], r[1], NAMED)
    counts = [0, 0, 0]
    for c in (UNEX, EXPL, NAMED):
        counts[c] = cls.count(c)

    def runs_of(pattern):
        return [(mt.end() - mt.start(), DATA_START + mt.start(), DATA_START + mt.end())
                for mt in re.finditer(pattern, bytes(cls))]

    unex = sorted(runs_of(rb"\x00+"), reverse=True)
    unnamed = sorted(runs_of(rb"[\x00\x01]+"), reverse=True)

    print("FFX.exe .data 0x%X..0x%X, %d bytes. Initialized data ends at 0x%X, zero fill from there"
          % (DATA_START, DATA_END, total, m.init_end))
    print("relocations %d, %d into .data, %d distinct targets, %d points (%d user named), %d literals"
          % (d["fixups_total"], len(d["refs"]), len(m.refs_to), len(m.real),
             sum(1 for a in m.real if m.points[a][2]), len(d["literals"])))
    print("named %d bytes (%.1f%%), explained %d (%.1f%%), unexplained %d (%.1f%%)"
          % (counts[NAMED], 100.0 * counts[NAMED] / total, counts[EXPL], 100.0 * counts[EXPL] / total,
             counts[UNEX], 100.0 * counts[UNEX] / total))
    print("unexplained runs over 0x%X: %d" % (min_gap, sum(1 for r in unex if r[0] > min_gap)))
    print()
    print("largest unexplained runs")
    for n, a, b in unex[:nruns]:
        prev = m.real[bisect.bisect_right(m.real, a) - 1] if bisect.bisect_right(m.real, a) else None
        pn = m.points[prev][1] if prev is not None else ""
        print("  0x%07X..0x%07X %8X  after %s" % (a, b, n, pn or ("0x%X" % prev if prev else "")))
    print()
    print("unnamed runs over 0x%X and what explains them" % min_gap)
    for n, a, b in sorted((r for r in unnamed if r[0] > min_gap), key=lambda r: r[1]):
        regs = m.regions_over(a, b)
        if not regs:
            print("  0x%07X..0x%07X %8X  %s, referenced items, see the table" % (a, b, n, m.subsystem(a)[0]))
            continue
        print("  0x%07X..0x%07X %8X" % (a, b, n))
        for r in sorted(regs, key=lambda r: (r[0], -(r[1] - r[0]))):
            print("      0x%07X..0x%07X [%s] %s" % (r[0], r[1], r[4], r[6]))
    print()

    # subsystem totals and runs. The bytes from each point to the next belong to that point,
    # except inside a curated region, which owns its bytes unless a user named point inside it
    # says otherwise
    reach = m.reach()
    sub_bytes = {}
    sub_named = {}
    seq = []
    for x, y, sub in m.owner_intervals():
        sub_bytes[sub] = sub_bytes.get(sub, 0) + (y - x)
        if seq and seq[-1][2] == sub and seq[-1][1] == x:
            seq[-1][1] = y
        else:
            seq.append([x, y, sub])
    for a in m.real:
        if m.points[a][2]:
            s2 = m.subsystem(a)[0]
            sub_named[s2] = sub_named.get(s2, 0) + 1
    print("bytes by subsystem")
    for s2, n in sorted(sub_bytes.items(), key=lambda kv: -kv[1]):
        print("  %-28s %10d  %5.1f%%  %4d named" % (s2, n, 100.0 * n / total, sub_named.get(s2, 0)))
    print()
    print("subsystem runs of 64 KB or more")
    for x, y, s2 in seq:
        if y - x >= 0x10000:
            print("  0x%07X..0x%07X %8X  %s" % (x, y, y - x, s2))
    print()
    print("thread reach: " + ", ".join("%s %d" % (t, len(v)) for t, v in sorted(reach.items())))
    print()
    print("named globals with a direct writer that a non-step root reaches (W R J A P L V T M)")
    for a in m.real:
        p = m.points[a]
        if not p[2]:
            continue
        tags, writers = m.writer_threads(a)
        off = "".join(t for t in tags if t in "WRJAPLVTM")
        if off:
            wn = ", ".join(sorted({m.funcs.get(w, "nofunc") for w in writers if w != -1}))
            print("  0x%07X %-44s %-6s %s" % (a, p[1], tags, wn[:90]))
    print()
    print("proposed region names not in the IDB yet (tools/apply_datamap_names.py)")
    for r in REGIONS:
        if r[2] and m.idb_name(r[0]) != r[2]:
            print("  0x%07X %s (IDB has %s)" % (r[0], r[2], m.idb_name(r[0]) or "nothing"))

    if table_out:
        write_table(m, rows, table_out)
        print("wrote %s" % table_out)


def write_table(m, rows, out):
    L = []
    L.append("# FFX.exe .data, every named global")
    L.append("")
    L.append("Generated by `tools/datamap.py report --table`. Do not edit by hand, rerun it. The method and the")
    L.append("explained runs are in `DATA_MAP.md`.")
    L.append("")
    L.append("Columns. **size** is the global's own size and **src** where it came from: `type` the IDB type,")
    L.append("`scalar` the access width, `region` a curated region in datamap.py (its tag is in DATA_MAP.md")
    L.append("section 3), `base` an indexed or address-taken base whose extent runs to the next referenced")
    L.append("address (**span**, [I]). **sub** is the owning subsystem and how it was decided: `n` from the")
    L.append("name, `g` from a region, `r` from the functions that reference it. **w** is which threads write it")
    L.append("directly, by direct call reachability from the thread roots [M]: S step (FFX_MainStep), I init")
    L.append("(FFX_MainInit, FFX_BootSequence), W main thread outside the step, R render thread, J job worker,")
    L.append("A FMOD thread, P input thread, L async loader, V vsync thread, M movie threads, ? a writer no")
    L.append("root reaches directly (called through a pointer), - no direct write. **r/w/o** counts the")
    L.append("relocated reads, writes and address takes (offsets, pushes, data pointers). Compiler RTTI type descriptors (`??_R0...`) are collapsed into runs.")
    L.append("")
    L.append("| address | name | size | src | span | sub | w | r/w/o | writers |")
    L.append("|---|---|---|---|---|---|---|---|---|")
    rtti = None

    def flush_rtti():
        if rtti:
            L.append("| 0x%X | %d RTTI type descriptors `??_R0...` | 0x%X | type | 0x%X | Phyre engine n | - | | |"
                     % (rtti[0], rtti[2], rtti[1] - rtti[0], rtti[1] - rtti[0]))
    for a, nxt, own, base in rows:
        p = m.points[a]
        if not p[2]:
            continue
        name = p[1]
        if name.startswith("??_R0"):
            if rtti and rtti[1] == a:
                rtti[1] = nxt
                rtti[2] += 1
            else:
                flush_rtti()
                rtti = [a, nxt, 1]
            continue
        flush_rtti()
        rtti = None
        reg = None
        for r in REGIONS:
            if r[0] == a and r[2] == name:
                reg = r
        if reg:
            size, src = reg[1] - reg[0], "region"
        elif p[3] > 1 and p[4] != "undef":
            size, src = own, "type"
        elif base:
            size, src = nxt - a, "base"
        else:
            size, src = own, "scalar"
        s, how = m.subsystem(a)
        tags, writers = m.writer_threads(a)
        nr = sum(1 for r in m.refs_to.get(a, []) if r[5] == "r")
        nw = sum(1 for r in m.refs_to.get(a, []) if r[5] == "w")
        no = len(m.refs_to.get(a, [])) - nr - nw
        wn = ", ".join(sorted({m.funcs.get(w, "nofunc") for w in writers if w != -1} | ({"nofunc"} if -1 in writers else set())))
        if len(wn) > 70:
            wn = wn[:67] + "..."
        L.append("| 0x%X | `%s` | 0x%X | %s | 0x%X | %s %s | %s | %d/%d/%d | %s |"
                 % (a, name, size, src, nxt - a, s, {"name": "n", "region": "g", "readers": "r"}.get(how, "-"),
                    tags, nr, nw, no, wn))
    flush_rtti()
    L.append("")
    open(out, "w", newline="\n").write("\n".join(L))


def main():
    import argparse
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    a = sp.add_parser("dump")
    a.add_argument("--idb", required=True, help="a COPY of the database")
    a.add_argument("--out", required=True)
    b = sp.add_parser("report")
    b.add_argument("--dump", required=True)
    b.add_argument("--table", help="write the per-global markdown table here")
    b.add_argument("--runs", type=int, default=25)
    b.add_argument("--min-gap", type=lambda s: int(s, 0), default=0x10000)
    args = ap.parse_args()
    if args.cmd == "dump":
        dump(args.idb, args.out)
    else:
        report(args.dump, args.table, args.runs, args.min_gap)


if __name__ == "__main__":
    main()
