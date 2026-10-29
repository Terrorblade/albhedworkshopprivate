// ffx_types.h - FINAL FANTASY X HD Remaster (FFX.exe) game-side structs.
//
// These are the ported PS2 structs, not PhyreEngine ones. PhyreEngine layouts come from the
// engine's own reflection metadata and live in phyre_types.h. There is no reflection for the
// game structs, so everything here was recovered by reading the accessor functions in the
// chr.c module (the dense getter/setter block at 0x82A930-0x82BA40) plus FFX_Ch_Allocate
// 0x824F90 and FFX_Ch_LoadChrData 0x825A40.
//
// Naming: a field named m_<something> is understood. A field named m_f/m_i/m_w/m_b followed by
// its hex offset is a real field at a confirmed offset and size whose meaning is not known yet.
// _unk_<hex> runs are not padding, they are stretches nothing has told us about yet. Sizes are
// pinned: sizeof(CHR) == 0x880 is printed by FFX_Ch_Init, sizeof(CHRDATA) == 300 is the stride
// of g_ffxChrDataTable.
//
// The battle records (BTLCHR, BTLHIT, BTLHITLOG, BTLUNIT) at the end come from the battle module, see
// BATTLE_ACTOR.md. sizeof(BTLUNIT) == 0xF90 and sizeof(BTLCHR) == 0x390 are pinned by FFX_Battle_GetActor
// 0x794020 and the allocator FFX_Btl_AllocActorArrays 0x793530.
//
// The save block records (SAVEDATA, SAVE_CHAR_RECORD, SAVE_EQUIP_ENTRY, SAVE_CHAR_NAME) at the very end
// are generated from tools/saveblock_fields.py, see SAVE_BLOCK_MAP.md. sizeof(SAVEDATA) == 0x68C0 is the
// memcpy size of FFX_SaveFile_InstallBlock 0x8B54A0.
//
// Addresses are VAs at the preferred base 0x400000. The exe is ASLR'd, see ../README.md.

struct CHR;
struct CHRDATA;
struct CHRPART;
struct CHRANIMF;
struct BTLCHR;
struct BTLHIT;
struct BTLHITLOG;
struct BTLUNIT;
struct SAVE_EQUIP_ENTRY;
struct SAVE_CHAR_RECORD;
struct SAVE_CHAR_NAME;
struct SAVEDATA;

// CHRANIMF   size=0x0C (12)
// A one-float animated value. FFX_Ch_AnimFloatSet(slot, value, mode) writes m_target and m_mode,
// and when mode == 0 it snaps m_cur too. Used for scale, alpha and a few other smoothed values.
struct CHRANIMF {
  /* 0x000 */ float m_cur;
  /* 0x004 */ float m_target;
  /* 0x008 */ int m_mode;     // 0 means apply immediately
};

// CHRPART   size=0x38 (56)
// One entry per model part. The array hangs off CHR.m_parts, the count is the u16 at
// CHR.m_partTable + 6.
struct CHRPART {
  /* 0x000 */ void *m_mesh;      // bone count is the u16 at m_mesh + 24
  /* 0x004 */ int m_i04;
  /* 0x008 */ int m_i08;
  unsigned __int8 _unk_C[0x8];
  /* 0x014 */ float m_alpha;             // FFX_Ch_ModelSetAlpha. 0.0 from the memset in FFX_Ch_BuildSkeletonInstance (0x8278D9), 1.0 only once FFX_Ch_ModelSetAlphaAll runs at spawn
  /* 0x018 */ unsigned __int8 m_hidden;  // FFX_Ch_ModelSetHide stores hide & 1 over the whole byte, FFX_Ch_ModelGetHide reads it sign extended
  unsigned __int8 _unk_19[0x3];
  /* 0x01C */ float m_swayPrevPos[3];  // last world position of this part's anchor joint, read and rewritten
                                       // every pass by FFX_Ch_StepSecondaryPhysics 0x822DE0 (base m_parts at
                                       // CHR+0x1BC, stride 56) to get the joint's motion for the sway
  unsigned __int8 _unk_28[0x4];  // no reader or writer found
  /* 0x02C */ void *m_boneBufA;  // 4 bytes per bone, allocated by sub_826340 0x826340 from the CDF data when m_flags1 bit 0x200000 is set
  /* 0x030 */ void *m_boneBufB;  // one byte per vertex, the sway vertex mask StepSecondaryPhysics fills
  /* 0x034 */ int m_boneBufBFilled;  // 0 until StepSecondaryPhysics has filled m_boneBufB, then 1 (0x822DE0 pass, store near the end of the part loop)
};

// CHRDATA   size=0x12C (300)
// Loaded character data. 40 of these live in the fixed table g_ffxChrDataTable 0x23C8D00
// (40 * 300 == 0x2EE0). A free entry has m_id == -1. Allocated by FFX_Ch_BlkAllocate 0x825760,
// found by id with FFX_Ch_FindChrData 0x825EA0.
// Sharing one CHRDATA between two live CHRs is the designed path, not a hack: FFX_Ch_LoadChrData
// 0x825A40 short-circuits on FFX_Ch_FindChrData, so a second FFX_Ch_Allocate of the same id gets
// the same pointer. Nothing ever stores through chr->m_data, and there is no back pointer to a CHR.
// FFX_Ch_DataDispose(id, assertFlag) 0x824F00 is SAFE with clones - it returns -1 early if any
// in-use CHR still has that id, so the data survives until the last one dies. An earlier version of
// this comment said never to call it, which was wrong.
// The two that really are dangerous are FFX_Ch_DataDisposeForce 0x8259F0 and
// FFX_Ch_DataDisposeAll 0x8259C0, which have no such guard.
struct CHRDATA {
  /* 0x000 */ int m_id;
  /* 0x004 */ void * m_partTable;   // part count is the u16 at +6
  unsigned __int8 _unk_8[0xC];
  /* 0x014 */ void * m_texData;
  // 0x018..0x06B are filled by FFX_Ch_InitChrDataFromBlob 0x825EE0 from the .chr header and its section 9
  // parameter block (sec9 below). The defaults are what it writes when the blob has no section 9.
  /* 0x018 */ void * m_texDataAux;   // blob+28, the section 1 aux word next to m_texData. No reader found
  /* 0x01C */ float m_collisionRadiusScale;   // sec9 +4, default 1.0. CHR.m_collisionRadiusScale takes this for a negative argument, FFX_Ch_GetDataCollisionRadiusScale 0x82AD70
  /* 0x020 */ float m_f20;   // sec9 +16, default 1.0. Read only through FFX_Ch_GetDataF20 0x82AD90, by maybe_FFX_Btl_SetupUnitChr 0x793738 and magic code (0x92C776, 0x92E3DD)
  /* 0x024 */ float m_f24;   // sec9 +24, default 1.0. The default of CHR.m_f4EC
  /* 0x028 */ float m_height;   // sec9 +20, default 0. The default of CHR.m_height
  /* 0x02C */ float m_rootPointLift;   // sec9 +8, default 8.0. The default of CHR.m_rootPointLift
  /* 0x030 */ float m_f30;   // sec9 +12, default 1.0. A length unit: FFX_Ch_BuildSkinMatrices scales a child's attach offset by
                             // the parent's m_f30 and the root by m_f30 / m_modelScale * 0.01, FFX_Ch_UpdateBonePositions
                             // uses 10000 / m_f30^2 * m_modelScale. Also read by magic code through FFX_Ch_GetDataF30 0x82AB60
  /* 0x034 */ float m_defaultScale;   // sec9 +28. FFX_Ch_Allocate seeds the CHR uniform scale from this
  /* 0x038 */ float m_modelScale;   // sec9 +32. Multiplies attach offsets
  /* 0x03C */ float m_f3C;   // 1000 / m_f30, default 1000. FFX_Ch_BuildSkinMatrices 0x832A90 divides it by CHR scale * CHR.m_rootScaleDivisor
  /* 0x040 */ unsigned __int8 m_lookAtMirror;   // sec9 +36, default 0 but 1 for category 2 (npc). FFX_Ch_UpdateLookAt 0x834D82 negates two look-at terms when it is 1
  unsigned __int8 _unk_41[0x1];
  /* 0x042 */ unsigned __int8 m_headSwayAxis;   // sec9 +38, default 2. FFX_Ch_UpdateBlink 0x8346BB adds the idle sway angle to head rotation X, Y or Z for 0, 1, 2
  /* 0x043 */ unsigned __int8 m_headSwayFlip;   // sec9 +39, default 1, 0 for npc. Non-zero negates the sway angle (FFX_Ch_UpdateBlink 0x834653)
  /* 0x044 */ float m_headRestRot[3];   // sec9 +40..+48. FFX_Ch_UpdateBlink writes these into head joint (bone 13) rotation floats 57..59 before adding the sway
  /* 0x050 */ float m_f50;   // sec9 +52, default 1.0. No reader found
  /* 0x054 */ int m_paramRevision;   // sec9 [0]. 5668 or more adds the 0x058..0x065 fields
  /* 0x058 */ float m_specIntensity;   // sec9 +56, default 1.0. The w lane of one FFX_ChrInstance_SetSpecParams vector in FFX_Ch_StepAll 0x82FC99. Name inferred
  /* 0x05C */ float m_specPower;   // sec9 +60, default 2.0. The w lane of the other one (0x82FCA2). Name inferred
  /* 0x060 */ unsigned int m_specColor; // sec9 +64, RGBA, default 0x7F808080, read by FFX_Ch_StepAll (CHRDATA.md)
  /* 0x064 */ unsigned __int8 m_b64;   // sec9 +68, default 0. No reader found
  /* 0x065 */ unsigned __int8 m_b65;   // sec9 +69, default 0. No reader found
  unsigned __int8 _unk_66[0x2];
  /* 0x068 */ void * m_p68;   // freed by FFX_Ch_DataFree 0x8258D0. No writer found in the chr module
  /* 0x06C */ void * m_blobBase;   // compared against a resource base by FFX_Ch_RelocateChrData
  /* 0x070 */ unsigned __int8 m_flags;   // bit 1 block allocated, bit 2 viewer data, bit 4 texture loaded
  /* 0x071 */ char m_name[32];   // e.g. "c001"
  /* 0x091 */ char m_name2[32];   // group/folder name
  unsigned __int8 _unk_B1[0x3];
  /* 0x0B4 */ const char * m_sourceTag;   // "Viewer" for viewer-loaded data
  /* 0x0B8 */ char m_path[64];   // e.g. "/ffx/proj2/chr/prot/<name2>/<name>"
  /* 0x0F8 */ unsigned __int8 m_isFallback;   // 1 when this is the c001 fallback substituted for a missing id
  unsigned __int8 _unk_F9[0x3];
  /* 0x0FC */ __int16 m_anmFrames;   // the standalone .anm clip header CHR.m_anmHeader points at (0x0FC..0x103): this word is the
                                     // clip's total frames, copied from the file by FFX_Ch_LoadAnmFile 0x8283C0 and FFX_Ch_DebugSpawnByName 0x8295E0
  unsigned __int8 _unk_FE[0x2];
  /* 0x100 */ void * m_anmFile;   // the loaded .anm file, the header's data word. Replaced by the same two, freed by FFX_Ch_DataFree
  /* 0x104 */ char * m_anmName;   // _strdup of the clip name, written by the same two
  /* 0x108 */ void * m_sysMotTbl;   // "<m_path>/inf/sysmot.tbl", read by sub_837FA0 0x837FA0 from FFX_Ch_LoadChrData (viewer data only)
  /* 0x10C */ void * m_relationTbl;   // "<m_path>/inf/relation.tbl", the same loader. FFX_Ch_SetMotionKey 0x837B3C reads it when m_flags bit 1 is clear
  /* 0x110 */ __int16 m_motRelCount;   // blob+52, the count of m_motRelTabs. FFX_Ch_SetMotionKey 0x837B05, FFX_Ch_LoadAnmFile 0x828561
  unsigned __int8 _unk_112[0x2];
  /* 0x114 */ void ** m_motRelTabs;   // blob+48, the motreltab array: entry[i] starts with the s16 model id it remaps. Same readers
  unsigned __int8 _unk_118[0x4];   // no reader or writer found
  /* 0x11C */ void * m_p11C;   // freed by FFX_Ch_DataFree 0x825986. No writer found in the chr module
  /* 0x120 */ void * m_effectDataSrc;
  /* 0x124 */ void * m_effectData;
  /* 0x128 */ int m_effectDataSize;
};

// CHR   size=0x880 (2176)
// The game actor. One flat array of these: g_ffxChrArray 0x23C44E4 holds the base pointer and
// g_ffxChrCount 0x23C44E0 the element count, so chr = g_ffxChrArray + 0x880 * i. Every loop in the
// game does exactly that and skips entries whose m_inUse is 0. Being in the array with m_inUse set
// is the whole registration - there is no separate per-frame or render sign-up call.
// 
// Movement is fully per-CHR: writing m_speed and m_moveDir and setting m_flags1 bit 0x400 is all it
// takes to drive any CHR. Only the input layer above that is a singleton (one pad, read into
// g_ffxControlledChr 0x1300788).
// 
// Pointer lifetime: the array base is written only by FFX_Ch_AllocChrArray 0x825670, so a CHR
// pointer is stable for the life of the pool. The pool is freed and reallocated on every map
// transition, so it is NOT stable across one. FFX_Ch_MoveChrMemory 0x826FA0 does not move CHRs, it
// compacts the heap blocks a CHR points at, so any pointer read out of a CHR must be re-read rather
// than cached.
// 
// FFX_Ch_AllocChrArray allocates 2 * count * 0x880. The upper half is not spare capacity: it is the
// shadow area FFX_Ch_CopyToScratchSlot 0x828B30 uses for map save and restore, so do not raise
// g_ffxChrCount to steal it. The sanctioned headroom is the +8 that maybe_FFX_Map_SetupChars
// 0x875510 adds when it sizes the pool (floor 36).
// 
// g_ffxTidusChr 0x12FBC60 caches the CHR whose CHRDATA name is "c001" or "c101" (Tidus).
// 
// Field naming pass 2026-10-13 (CHARACTER_MOTION.md section 12): "no reader found" below means none in the chr
// module (0x81F000..0x846000, every [reg+disp] and immediate checked against the base), none in any Hex-Rays
// function where the base is typed CHR, and none in the spot-checked external callers of FFX_Ch_*,
// FFX_Atel_GetActorChr and FFX_Battle_GetActor. The magic DLLs are outside the IDB and are not covered.
struct CHR {
  /* 0x000 */ __int16 m_id;   // chr id. (m_id >> 12) is the category: 0 pc, 1 mon, 2 npc, 3 sum, 4 wep, 5 obj, 6 skl, 0xF viewer prototype. Tidus is id 1 -> "c001"
  /* 0x002 */ unsigned __int8 m_inUse;   // 1 while this slot is live. FFX_Ch_Allocate scans this byte, every per-frame pass skips on it
  unsigned __int8 _unk_3[0x1];
  /* 0x004 */ const char * m_name;   // points at CHRDATA.m_name, e.g. "c001" for Tidus
  unsigned __int8 _unk_8[0x4];
  /* 0x00C */ float m_posX;
  /* 0x010 */ float m_posY;
  /* 0x014 */ float m_posZ;
  /* 0x018 */ float m_posW;
  /* 0x01C */ float m_prevPosX;   // FFX_Ch_SetPos and the motion step both mirror m_pos* into m_prevPos*
  /* 0x020 */ float m_prevPosY;
  /* 0x024 */ float m_prevPosZ;
  /* 0x028 */ float m_prevPosW;
  /* 0x02C */ float m_vec2C[4];
  /* 0x03C */ float m_walkmeshPos[4];   // pos * g_ffxWalkmeshScale, rewritten at the top of every
                                        // FFX_Ch_WalkmeshMove step (store at 0x83E67A) and used as the
                                        // segment origin for the edge tests. Per-frame scratch, not a
                                        // destination and not a last-safe-position. [3] is never written
  /* 0x04C */ float m_velX;   // NOT a persistent velocity: this is the displacement for the current
  /* 0x050 */ float m_velY;   // substep, written from m_speed and m_moveDir and zeroed again at
  /* 0x054 */ float m_velZ;   // 0x83D4B8 right after FFX_Ch_ResolveCollisionsAll consumes it
  unsigned __int8 _unk_58[0x4];
  /* 0x05C */ float m_scaleX;   // FFX_Ch_SetScaleMasked / FFX_Ch_GetScaleX
  /* 0x060 */ float m_scaleY;
  /* 0x064 */ float m_scaleZ;
  unsigned __int8 _unk_68[0x4];
  /* 0x06C */ float m_offsetX;   // the debug CHRInfo window labels this triple "Offset". Folded into the world matrix by FFX_Ch_BuildSkinMatrices
  /* 0x070 */ float m_offsetY;
  /* 0x074 */ float m_offsetZ;
  unsigned __int8 _unk_78[0x4];
  /* 0x07C */ float m_groundNormalX;   // up-normal of the walkmesh triangle under the CHR. Forced to (0,-1,0) when m_flags1 bit 0x80 ends up SET (test at 0x82AA93, corrected 2026-10-13)
  /* 0x080 */ float m_groundNormalY;
  /* 0x084 */ float m_groundNormalZ;
  unsigned __int8 _unk_88[0x4];
  /* 0x08C */ float m_triVert0[4];   // the three VERTICES of the CHR's current walkmesh triangle, in
  /* 0x09C */ float m_triVert1[4];   // walkmesh space. Written by FFX_Ch_WalkmeshTestEdgeCrossing at
  /* 0x0AC */ float m_triVert2[4];   // 0x83DC91..0x83DCF8 whenever a move stays inside its triangle.
                                     // FFX_Ch_UpdateShadeFromLights takes the 3D DISTANCE from
                                     // m_walkmeshPos to each (FFX_Vec3_Distance 0x8366A0), inverts it,
                                     // weights by the per-vertex shade in m_groundAttrs and feeds the
                                     // length to FFX_Ch_SetShadeCoeff. It is inverse-distance
                                     // interpolation of a baked per-vertex shadow value.
                                     // These were called m_lightDir0/1/2 and described as directions
                                     // that get dotted. Both halves of that were wrong
  /* 0x0BC */ float m_effectR;   // the debug CHRInfo window labels this triple "effectRGB". Multiplied into m_animShadeR/G/B before the CharacterShadeColor shader constant is written
  /* 0x0C0 */ float m_effectG;
  /* 0x0C4 */ float m_effectB;
  /* 0x0C8 */ float m_effectW;   // the fourth dword after m_effectR/G/B. FFX_Ch_CopyState 0x8286F2 copies it with them. No other reader or writer found
  unsigned __int8 _unk_CC[0x40];   // 0x0CC..0x10B: no reader or writer found. FFX_Ch_CopyState does not copy it.
                                   // Displacements in this range in the chr module all sit on joint nodes, CHRDATA or the GS packet builder
  /* 0x10C */ float * m_rootMatrixOverride;   // 4x4 matrix pointer. When set and m_parent is null, FFX_Ch_BuildSkinMatrices 0x832944
                                              // copies it into the root joint world matrix (0x1D0) instead of building one from
                                              // pos, rotY, tilt and scale. Written only by FFX_Ch_SetI10C 0x82B3E0, called from
                                              // battle and magic code (0x793E56 .. 0x92E92B, 11 sites) and magic host API 0xC65034,
                                              // read back by FFX_Ch_GetI10C 0x82AC60
  /* 0x110 */ float m_rootScaleDivisor;   // initialised to 1.0. FFX_Ch_BuildSkinMatrices 0x832AAD..0x832AD1 builds the second root matrix
                                         // (0x210) as the world matrix times CHRDATA.m_f3C / (m_scale* x this). Set only by FFX_Ch_SetF110
                                         // 0x82B400 (FFX_Ch_Allocate, magic host API 0xC64D64)
  unsigned __int8 _unk_114[0x40];   // 64 bytes, the size of a matrix. Only ever passed by address, as the second argument of
                                    // sub_82E3E0 0x82E3E0 (FFX_Ch_BuildBlobShadow 0x8302D9 and 0x830303, FFX_Ch_ShadowPassAll 0x83020D),
                                    // and sub_82E3E0 never reads that argument. No other reader or writer found.
                                    // Likely a PS2 shadow matrix the port stopped filling [I]
  /* 0x154 */ float m_speed;   // current movement speed, the debug window's "sp". FFX_Ch_AutoLocomotionAnim compares it against m_runThreshold to pick idle/walk/run
  /* 0x158 */ float m_rotY;   // current facing yaw in radians, the debug window's "rot". Slewed toward m_moveDir by the locomotion driver
  /* 0x15C */ float m_tiltX;   // current X tilt of the root in radians. FFX_Ch_BuildSkinMatrices 0x8329AD..0x8329F8 slews it toward
                               // m_tiltXTarget by m_tiltXStep with FFX_Mot_ApproachAngle and applies it as a root X rotation, only
                               // while m_tiltXTarget is non-zero. Copied by FFX_Ch_CopyState
  /* 0x160 */ float m_tiltXTarget;   // written by sub_8357F0 0x8357F0 (ATEL, 0xA79ADD) as a wrapped angle. A step of 0 snaps m_tiltX too
  /* 0x164 */ float m_tiltXStep;   // radians per step, sub_8357F0 converts its degree argument
  /* 0x168 */ float m_moveDir;   // desired movement direction in radians, the debug window's "dir". This plus m_speed is the entire movement input for any CHR
  /* 0x16C */ float m_groundHeight;   // ground height under the CHR, written by FFX_Ch_WalkmeshMove and consumed by the clamp at the end of FFX_Ch_UpdateMotionAll
  /* 0x170 */ float m_runThreshold;   // walk/run speed threshold, defaults to 18.0 when set with a value <= 0
  /* 0x174 */ float m_clipZ;   // the debug CHRInfo window prints this as "ClipZ" (value / 10). It is NOT the collision radius - that is m_collisionRadius at 0x4E8. Its setter scales by 100
  /* 0x178 */ float m_cameLen;   // OUTPUT, not a parameter. FFX_Ch_UpdateCameLenAndZClip 0x82E220
                                //   rewrites it every frame as the distance from m_bodyCentre (0x694,
                                //   not the origin) to the active camera eye, then sets hide bit 0x20
                                //   when it exceeds m_clipZ. Does not control the camera. The debug
                                //   CHRInfo window prints it as "CameLen" (value / 10)
  /* 0x17C */ float m_f17C;   // blob shadow parameter, 0.2 from FFX_Ch_Allocate. Written by sub_830CB0 0x830CB0 (also battle 0x7A436E,
                             // ATEL 0xA7A669), read only by sub_82E3E0 0x82E527 as the 7th argument of FFX_ChrShadow_WriteBlobQuad.
                             // Copied by FFX_Ch_CopyState
  /* 0x180 */ unsigned __int8 m_hideFlags;   // HIDE mask, 0 means fully visible. Bits from the "Hide   : %x" line: 0x01 Ev, 0x02 Eff, 0x08 Disp, 0x10 Btl, 0x20 Z. Every per-frame pass skips a CHR whose value is non-zero unless m_b181 is set
  /* 0x181 */ unsigned __int8 m_b181;   // process even while hidden - the per-frame passes accept the CHR when this is non-zero
  /* 0x182 */ unsigned __int8 m_groundMode;   // 0 clamp down to m_groundHeight, 1 hard snap, 2 water buoyancy against g_ffxWaterLevel
  /* 0x183 */ unsigned __int8 m_b183;   // 1 for categories 0-3, 0 for 4-6, 3 for 0xF
  /* 0x184 */ unsigned __int8 m_b184;   // field and weapon CHRs get 15
  /* 0x185 */ char m_b185;
  /* 0x186 */ unsigned __int8 m_locomotionMode;   // 0 ground/field -> FFX_Ch_AutoLocomotionAnim, 2 swim -> maybe_FFX_Ch_AutoAnimMode2
  /* 0x187 */ unsigned __int8 m_b187;   // written by sub_830160 0x830160 (magic host API 276), which also pushes it to byte +21 of m_instance. No reader found
  /* 0x188 */ unsigned __int8 m_lightOverrideMask;   // bit k (0..3) makes sub_836250 0x836250 use this CHR's m_lightColor and m_lightDir
                                                    // slot k instead of the global default when it builds the light matrices.
                                                    // Written by sub_830AE0 (magic host API 398) and sub_830B00, read by sub_830AD0
  unsigned __int8 _unk_189[0x3];
  /* 0x18C */ int m_partyIndex;   // party/character index, 255 means not a party member
  /* 0x190 */ int m_objId;   // field-object / group id, -1 when unset. A child caches its parent's in m_parentUid
  /* 0x194 */ unsigned int m_flags1;   // 0x40 externally driven clip: FFX_Ch_UpdateMotionAll skips the locomotion selector at 0x832E8C AND FFX_Mot_AdvanceFrame skips the whole sequence VM, 0x80 free move (FFX_Ch_ResolveCollisionsAll 0x83D476 skips FFX_Ch_WalkmeshMove, no ground clamp, normal forced to (0,-1,0), CHARACTER_MOTION.md), 0x100 no body collision, 0x200 THIS CHR IS THE PLAYER (maintained by FFX_Ch_UpdateRenderJob), 0x400 turn toward m_moveDir, 0x4000 bumped this frame, 0x8000 bumped last frame, 0x10000 blend settled, 0x20000 running, 0x40000 idshade, 0x200000 needs a cloned ClassCharacter, 0x1000000 needs a skin rebuild, 0x2000000 moved, 0x4000000 idle, 0x8000000 moving, 0x20000000 head under water (posY - 8.5 > water level with +Y down, corrected 2026-10-13, CHARACTER_MOTION.md), 0x40000000 height-aware collision, 0x80000000 raw .anm clip through m_anmHeader, with 0x40 (corrected 2026-10-27, free move is 0x80, BTLMAP_FORMAT.md). Bits 0x07000000 are cleared every frame by FFX_Ch_ClearFlags1StepBits
  /* 0x198 */ unsigned int m_flags2;   // 0x2 use m_windYaw/Pitch/Strength, 0x4, 0x8, 0x20 skip the render pass, 0x80, 0x200, 0x400 inside FFX_Ch_Allocate. 0xC00 exempts the CHR from FFX_Ch_MoveChrMemory
  /* 0x19C */ struct CHR * m_parent;   // set by FFX_Ch_AttachToParentBone. Children are found by scanning the array for m_parent == this
  /* 0x1A0 */ int m_parentJoint;   // joint/bone index on the parent
  /* 0x1A4 */ float m_attachOffX;   // attach offset * 100 * CHRDATA.m_modelScale
  /* 0x1A8 */ float m_attachOffY;
  /* 0x1AC */ float m_attachOffZ;
  /* 0x1B0 */ float m_attachOffW;   // always 1.0
  unsigned __int8 _unk_1B4[0x4];
  /* 0x1B8 */ int m_parentUid;   // copy of the parent's m_objId, -1 when unparented
  /* 0x1BC */ struct CHRPART * m_parts;   // per-model-part array, CHRPART stride 56
  /* 0x1C0 */ void * m_skeleton;   // copy of CHRDATA.m_partTable. +6 mesh count, +8 skin flag, +10 joint count, +28 joint table, +44 collision count
  /* 0x1C4 */ struct CHRDATA * m_data;   // loaded character data, 300 bytes, an entry of g_ffxChrDataTable
  /* 0x1C8 */ unsigned __int8 m_rootJoint[352];   // embedded root joint node, same 352-byte layout as the entries of m_joints. 0x1C8 + 352 == 0x328 exactly. The CHR world matrix sits at 0x1D0 and the scaled one at 0x210
  /* 0x328 */ void * m_joints;   // joint node array, stride 352, allocated by FFX_Ch_BuildSkeletonInstance. Relocated by FFX_Ch_MoveChrMemory
  /* 0x32C */ void * m_collisionVolumes;   // 96 bytes each. Relocated by FFX_Ch_MoveChrMemory
  /* 0x330 */ struct CHRANIMF m_animShadeR;   // the seven animators at 0x330..0x383 are stepped by FFX_Ch_StepAnimFloat. The first three become the CharacterShadeColor shader constant, multiplied by m_effectR/G/B
  /* 0x33C */ struct CHRANIMF m_animShadeG;
  /* 0x348 */ struct CHRANIMF m_animShadeB;
  /* 0x354 */ struct CHRANIMF m_animShadeA;   // m_cur must be non-zero for FFX_Ch_UpdateRenderAll to touch the CHR, so 0 reads as invisible
  /* 0x360 */ struct CHRANIMF m_animBrightness;   // the debug window's "P" (SG_DebugGui_ChrInfoWindowProc 0x8537D0). While m_cur != m_target
                                                   // FFX_Ch_StepAll 0x82F5C0..0x82F5D5 multiplies the material RGB by m_cur, once it settles the
                                                   // factor is 1.0 again, so it only shows during a fade. Set by FFX_Ch_SetBrightness 0x82B5B0 (ATEL 0xA78F6D)
  /* 0x36C */ struct CHRANIMF m_animShadeCoeff;   // pushed to the CharacterShadeCoeff shader constant by FFX_Ch_SetShadeCoeff
  /* 0x378 */ struct CHRANIMF m_anim378;   // stepped by FFX_Ch_StepAnimFloat with the rest, set by FFX_Ch_SetAnim378 0x82B860 (FFX_Ch_Allocate,
                                           // battle 0x796395 and 0x7963CD). No reader of m_cur or m_target found
  /* 0x384 */ float m_lightColor[16];   // 4 float4 point-light colour sums. FFX_Ch_AccumulateLights 0x8305A0 resets each to (0,0,0,1),
                                       // then every active light in range adds colour * falloff into slot (light byte +37). Read by
                                       // sub_836250 0x836250 under m_lightOverrideMask
  /* 0x3C4 */ float m_lightDir[16];   // 4 float4 summed directions from m_lightPoint toward those lights, the same writer and reader
  unsigned __int8 _unk_404[0x4];   // no reader or writer found
  /* 0x408 */ int m_i408;   // initialised to 1, and set to 1 again by FFX_Ch_RegisterShadowRegistryEntry. Read only through FFX_Ch_GetI408
                            // 0x82A9A0 (battle 0x794E6E, btl property 87, ATEL Core 481, magic host API 7)
  /* 0x40C */ unsigned __int8 m_lookAtEnabled;   // sub_834F60 0x834F60 (ATEL 0xA79F92, battle). Turning it on sets m_lookAtWeightTarget to 1, off to 0
  /* 0x40D */ unsigned __int8 m_lookAtKind;   // requested kind: 1 camera, 2 the CHR at m_lookAtTarget, 3 a fixed point, 4 a fixed yaw and Z angle in m_lookAtTarget[0], [1]
  /* 0x40E */ unsigned __int8 m_lookAtKindApplied;   // sub_834F60 copies m_lookAtKind here. When they differed it snapshots m_lookAtMatrix into m_lookAtPrevQuat and sets m_lookAtSwitchBlend
  unsigned __int8 _unk_40F[0x5];   // no reader or writer found
  // 0x414..0x4D3 are FFX_Ch_UpdateLookAt 0x8348D0 state, it drives the head joint (bone 9) through FFX_Ch_SetJointOverrideMatrix
  /* 0x414 */ float m_lookAtWeightTarget;
  /* 0x418 */ float m_lookAtWeight;   // eased toward the target by 1 / m_lookAtBlendFrames per call, snapped to 1 above 0.99 and to 0 below 0.01. 0 turns the override off
  /* 0x41C */ float m_lookAtYaw;   // current head yaw in DEGREES, eased toward the clamped goal. sub_834E60 seeds it with pi/2 as if it were radians
  /* 0x420 */ float m_lookAtBlendFrames;   // 6.0 by default, sub_835020 0x835020 (FFX_Ch_Allocate, ATEL 0xA7A5F9), 0 means 6
  /* 0x424 */ float m_lookAtYawMin;   // 20 degrees, set with the max by sub_834FD0 0x834FD0 from sub_834E60 0x834E60 (FFX_Ch_Allocate)
  /* 0x428 */ float m_lookAtYawMax;   // 160 degrees
  /* 0x42C */ float m_lookAtRestMatrix[16];   // the head joint's rest matrix, built once by sub_834E60. Post-multiplies every look-at matrix
  /* 0x46C */ float m_lookAtMatrix[16];   // this step's head override matrix
  /* 0x4AC */ float m_lookAtPrevQuat[4];   // quaternion of m_lookAtMatrix taken when the kind changes, slerped from while m_lookAtSwitchBlend > 0
  /* 0x4BC */ float m_lookAtSwitchBlend;   // 1.0 on a kind change, minus 0.25 per call
  /* 0x4C0 */ float m_lookAtTarget[4];   // target CHR pointer or point, read by FFX_Ch_UpdateLookAt
  /* 0x4D0 */ int m_lookAtSide;   // which yaw limit last clamped (1 the min, 0 the max), picks the -15 or +15 degree hysteresis
  /* 0x4D4 */ unsigned __int8 m_blinkState[16];   // idle head-sway and blink state, FFX_Ch_UpdateBlink
  /* 0x4E4 */ float m_collisionRadiusScale;   // negative argument means take CHRDATA.m_collisionRadiusScale
  /* 0x4E8 */ float m_collisionRadius;   // effective radius, recomputed each step as (m_scaleZ + m_scaleX) * 0.5 * m_collisionRadiusScale
  /* 0x4EC */ float m_f4EC;   // negative argument means take CHRDATA.m_f24. Battle only: sub_794A70 0x794A70 returns it times the battle
                             // unit's float +0x3A8 (btl getter case 11, FFX_AtelSys_Btl_091_resf), btl property 270 sets it through
                             // FFX_Ch_SetF4EC. Getter FFX_Ch_GetF4EC 0x82ADB0 is magic host API 0xC6529C. Meaning open
  /* 0x4F0 */ float m_height;   // negative argument means take CHRDATA.m_height. The unit height: maybe_FFX_Btl_GetUnitHeight 0x7950F0
                               // returns it times the battle unit scale +0x3A4, and maybe_FFX_Btl_GetUnitCenterPos 0x794C20 is
                               // pos.y - height / 2. Btl property 272. Was m_f4F0
  /* 0x4F4 */ float m_rootPointLift;   // negative argument means take CHRDATA.m_rootPointLift. FFX_Ch_UpdateBonePositions subtracts it
                                      // from bone 0's y to make m_rootPoint (+Y is down, so it raises the point). Battle reads it
                                      // through FFX_Ch_GetF4F4 0x82AB30 (sub_794A30, FFX_AtelSys_Btl_092_resf), btl property 271. Was m_f4F4
  /* 0x4F8 */ float m_f4F8;   // written only by FFX_Ch_SetF4F8 0x82B320 (ATEL Ch_010 0xA78C4C) and FFX_Ch_CopyState. No reader found
  /* 0x4FC */ float m_waterFloatDepth;   // ground mode 2 eases it toward max(m_feetDepth, m_collisionRadiusScale) over 30 sub-steps
                                        // and floats the CHR that far above m_groundHeight (FFX_Ch_UpdateMotionAll 0x83316D..0x833185)
  /* 0x500 */ float m_pushWeight;   // initialised to 1.0. FFX_Ch_SeparatePair 0x83ECD0 splits a push by it, negative means immovable,
                                   // both negative means whoever moved more yields (CHARACTER_MOTION.md 4.2). Was m_f500
  /* 0x504 */ float m_vertVel;   // vertical velocity, used by the water buoyancy branch of the ground clamp
  unsigned __int8 _unk_508[0x8];   // no reader or writer found. FFX_Mot_AdvanceFrame's stores at 0x838F3E/0x838F4A are on a joint node, not here
  /* 0x510 */ float m_bodyHeight;   // |m_headPoint.y - m_bodyCentre.y|, written by FFX_Ch_UpdateBonePositions 0x8350F0 (0x835483)
  /* 0x514 */ float m_feetDepth;   // |m_posY - m_bodyCentre.y|, the same writer (0x8354A1). Read by ground mode 2 (0x83314A)
  unsigned __int8 _unk_518[0x4];   // no reader or writer found
  /* 0x51C */ float m_collisionHeight;   // when non-zero the collision band top is m_posY - this. FFX_Ch_SetF51C 0x82B340 (ATEL Ch_128). Was m_f51C
  /* 0x520 */ float m_collisionTopY;   // the band top, written in FFX_Ch_ResolveCollisionsAll 0x83D4F9, read by its pair test 0x83D5D9
  /* 0x524 */ float m_boneWorldPos[88];   // 22 float4 bone world positions, stride 16. The w slot of each is the valid marker. 0x524 + 22*16 == 0x684 exactly
  /* 0x684 */ float m_prevBodyCentre[4];
  /* 0x694 */ float m_bodyCentre[4];
  // 0x6A4..0x6D3 are written by FFX_Ch_UpdateBonePositions 0x8350F0 after m_boneWorldPos (CHARACTER_MOTION.md 4.3)
  /* 0x6A4 */ float m_headPoint[4];   // (m_bodyCentre.x, bone 1 y, m_bodyCentre.z, bone 1 w). [1] is the collision band top when
                                     // m_collisionHeight is 0. Bone 1 as the head is inferred. Was m_vec6A4
  /* 0x6B4 */ float m_rootPoint[4];   // bone 0 position with y -= m_rootPointLift. Read by ATEL Ch_056 0xA7A2C9 and mode 2 of
                                     // sub_A7A980 0xA7AA21, which hand it to the script. Was m_vec6B4
  /* 0x6C4 */ float m_lightPoint[4];   // (m_bodyCentre.x, feet y - 0.8 * m_bodyHeight, m_bodyCentre.z, w), the point
                                      // FFX_Ch_AccumulateLights 0x8305A0 measures light distance and direction from
  unsigned __int8 _unk_6D4[0x8];   // no reader found. FFX_Ch_CopyState copies the dword at 0x6D8
  // 0x6DC..0x6F7 are the texture animation lists sub_83CDD0 0x83CDD0 builds from CHRDATA.m_texData (version 0x1029 or
  // later) inside m_texAnimBlock: n slots of 32 bytes, then list A and list B of 64-byte entries whose +48 is a data
  // pointer and +54, +58 u16. sub_8313F0 0x8313F0 patches each slot's +20 low 14 bits from a list B entry and +24 bits
  // 7..20 from a list A entry (GS texture and CLUT base fields, inferred). Rebuilt by FFX_Ch_MoveChrMemory
  /* 0x6DC */ void * m_texAnimBlock;   // the heap block, allocated by sub_83D0A0 0x83D0A0 from FFX_Ch_Allocate, freed by FFX_Ch_Dispose. Was m_p6DC
  /* 0x6E0 */ void * m_texAnimSlots;   // block base, m_texAnimSlotCount entries of 32
  /* 0x6E4 */ void * m_texListA;   // after the slots, m_texListACount entries of 64. sub_831390 / sub_8311D0 (magic host API 356, 354) drive it
  /* 0x6E8 */ __int16 m_texListACount;   // CHRDATA.m_texData u16 [4]
  /* 0x6EA */ __int16 m_texListATotal;   // sum of the entries' u16 +54
  /* 0x6EC */ void * m_texListB;   // after list A, m_texListBCount entries of 64. sub_8313C0 / sub_831300 (magic host API 357, 355)
  /* 0x6F0 */ __int16 m_texListBCount;   // CHRDATA.m_texData u16 [6]
  /* 0x6F2 */ __int16 m_texListBTotal;   // sum of the entries' u16 +54
  /* 0x6F4 */ int m_texAnimSlotCount;   // CHRDATA.m_texData u16 [2]
  unsigned __int8 _unk_6F8[0x4];   // no reader or writer found
  /* 0x6FC */ void * m_texAnimProgram;   // CHRDATA.m_texData dword +32 when its version is 0x1126 or later. Non-null enables the
                                       // texture animation calls (FFX_Ch_SetTexAnimPattern 0x83D040, FFX_Ch_UpdateBlink, the TEX
                                       // motion event, FFX_Ch_Dispose's free). Was m_i6FC
  /* 0x700 */ int m_texAnimHandle;   // slot index in the 64-entry table at 0x2310000 returned by movie_str__f77C950 0x77C950, -1 when
                                     // full. First argument of FFX_TexAnim_SetPattern and sub_77E940. Searched by FFX_Ch_FindByI700
                                     // 0x835ED0, relocated through sub_77E9F0 / sub_77EA10 by FFX_Ch_MoveChrMemory. Was m_i700
  /* 0x704 */ void * m_texListACopy;   // a private copy of list A's data (16-byte headers then the data) made by sub_83C710 0x83C710
                                      // (magic host API 345), freed by sub_83C810 0x83C810 (magic host API 346, FFX_Ch_Dispose). Was m_i704
  /* 0x708 */ unsigned __int8 m_texListACopyActive;   // sub_83C940 0x83C940 points every list A entry's +48 at the copy when 1 and at
                                                     // the original when 0, and stores the flag here. FFX_Ch_MoveChrMemory 0x82716B and
                                                     // sub_83CDD0 re-apply it. Was m_p708, a byte, never a heap block
  unsigned __int8 _unk_709[0x3];
  /* 0x70C */ void * m_motionEntry;   // current motion table entry: +0 WORD motion index, +12 sequence bytecode
  /* 0x710 */ void * m_motionGroup;   // motion group bank header: +8 WORD entry count, +12 entries, +16 PLAY base
  /* 0x714 */ void * m_seqPC;   // motion sequence bytecode program counter. Non-null means a motion is active
  /* 0x718 */ int m_clipStart;   // clip start frame, 24.8 fixed point
  /* 0x71C */ int m_clipEnd;   // clip end frame minus 1, 24.8
  /* 0x720 */ int m_clipSpan;   // end minus start, the loop rewind amount
  /* 0x724 */ __int16 m_loopCount;   // 1 means play once and hold. 0 means LOOP FOREVER, which is
                                     // what idle, walk and run all use. FFX_Mot_WrapFrameAtEnd only
                                     // takes the stop branch on "cmp word [eax+724h], 1 / jnz" at
                                     // 0x839319, so any other value just wraps without decrementing
  /* 0x726 */ __int16 m_pendingLoopCount;   // -1 means none
  /* 0x728 */ __int16 m_clipPlaying;   // sequence opcode 2 waits for this to reach 0
  /* 0x72A */ __int16 m_seqWait;   // sequence WAIT-N countdown
  /* 0x72C */ unsigned int m_motionId;   // current motion id, (modelId << 16) | motionIndex. The debug window's "Motion"
  /* 0x730 */ int m_i730;   // zeroed by FFX_Ch_SetMotionKey, set to 1 by FFX_Ch_ResetBlock730. Possibly a queued motion id
  /* 0x734 */ unsigned __int8 m_seqStopped;
  unsigned __int8 _unk_735[0x1];
  /* 0x736 */ unsigned __int8 m_motionRequestActive;   // the locomotion selectors only act while this is 0
  /* 0x737 */ __int8 m_seqWaitKey;   // gate for sequence opcode 6 (WAIT_KEY), and it is read SIGNED:
                                     // FFX_Mot_SeqExec does "movsx cx, byte [ebx+737h] / cmp cx, ax /
                                     // jl" at 0x8379FD. So the 255 that FFX_Ch_SetMotionKey seeds
                                     // reads back as -1 and blocks the opcode even at threshold 0.
                                     // Raised only by FFX_Ch_SetSeqWaitKey 0x837570, from the event
                                     // script opcodes at 0xA78710 and 0xA79550.
                                     // Was called m_seqBranchValue, which was wrong twice over: it is
                                     // a gate rather than a branch compare, and it is not unsigned
  /* 0x738 */ unsigned __int8 m_b738;   // set to 1 by the public motion setters
  unsigned __int8 _unk_739[0x1];
  /* 0x73A */ __int16 m_blendFrames;   // hokan frames for the clip being started
  /* 0x73C */ __int16 m_nextHokan;   // pending next blend, the debug window's "nexthokan". -1 maps to 0 and 0 maps to 1
  /* 0x73E */ unsigned __int8 m_b73E;   // set by FFX_Ch_SetMotionKey, cleared by the sequence PLAY opcode
  unsigned __int8 _unk_73F[0x1];
  /* 0x740 */ int m_frame;   // current frame, 24.8 fixed point, live
  /* 0x744 */ int m_frameSnapshot;   // frame handed to the renderer, the events and the debug window
  /* 0x748 */ void * m_channels;   // per-joint animated channel array, stride 396 (9 channels x 44). Relocated by FFX_Ch_MoveChrMemory
  /* 0x74C */ void * m_streamList;   // head of the streamed-channel list. Each node's +8 and +12 are relocated with the motion bundle
  /* 0x750 */ __int16 m_speedMulA;   // 8.8 fixed point, 256 = 1.0, initialised to 256
  /* 0x752 */ __int16 m_hokanCountdown;   // the debug window's "hokan"
  /* 0x754 */ __int16 m_playSpeed;   // 8.8 fixed point playback speed, 256 = 1.0
  /* 0x756 */ __int16 m_frameDelta;   // last computed frame delta, 24.8
  /* 0x758 */ void * m_jointRemap;   // motreltab joint remap, relocated when the CHRDATA blob moves
  /* 0x75C */ unsigned int m_slots[32];   // motion slot table, 0x80 bytes (FFX_Ch_CopyState memcpys exactly that). [0] idle, [1] walk, [2] run, [16] swim idle, [18] swim submerged, [19] swim surface. Seeded 0/1/2 and 16..19 = -1 by FFX_Ch_Allocate, where -1 falls back to g_ffxChrSlotDefaults. A slot whose low word is >= 0x1000 is a full motion id, otherwise it is a logical index
  /* 0x7DC */ int m_frameCursor;   // integer frame already fed to the key decoder
  /* 0x7E0 */ int m_frameRateScale;   // clip frame-rate scale, 7680 = 30 << 8
  /* 0x7E4 */ int m_eventCursor;   // -1 means reset
  /* 0x7E8 */ void * m_motionBundle;   // the .mgrp BUNDLE HEADER this CHR's current motion came from.
                                       // Written once, by FFX_Ch_SetMotionKey at 0x837CCD. The only
                                       // other users compare it against a bundle being released or
                                       // moved (FFX_Ch_ReleaseResource 0x836ECA,
                                       // FFX_Ch_RelocateResourceBlock 0x837147), both of which walk the
                                       // whole CHR array, so releasing a motion set stops every CHR
                                       // using it. Was called m_eventList, which was wrong - the real
                                       // event array is m_eventArray at 0x7F4
  /* 0x7EC */ unsigned __int8 m_anmJointCopyMode;   // read only by FFX_Mot_EvalAnmClip 0x8388DE: 1, 2, 3 copy one joint's decoded .anm
                                                   // keys onto the next one or two joints (joint 24 or 42). No writer found, so 0 in practice
  /* 0x7ED */ unsigned __int8 m_motionMissing;   // FFX_Ch_SetMotionKey 0x837C03 sets 1 when the chain walk finds no bank, its word store
                                                // at 0x837CDD clears this and m_b7EE on success. No reader found. The port's motionMissing
  /* 0x7EE */ unsigned __int8 m_b7EE;   // cleared with m_motionMissing, nothing else touches it
  unsigned __int8 _unk_7EF[0x1];
  /* 0x7F0 */ __int16 m_eventCount;
  /* 0x7F2 */ __int16 m_lastEventFrame;   // the Motev log's frame_lastev
  /* 0x7F4 */ void * m_eventArray;   // stride 8 + 4n. byte0 type (1 SND, 2 EFF, 3 TEX, 5 BTL), byte1 payload dword count, +2 start, +4 end, +6 id
  /* 0x7F8 */ void * m_effCallback;
  /* 0x7FC */ void * m_effContext;
  /* 0x800 */ void * m_anmHeader;   // standalone .anm clip header: [0] total frames, [1] data. Selected by m_flags1 bit 0x80000000
  // 0x804..0x81F tune FFX_Ch_StepSecondaryPhysics 0x822DE0, the joint sway (hair, cloth) that runs on a job worker for
  // CHRs with m_flags1 0x200000 and 0x400000
  /* 0x804 */ float m_windYaw;   // degrees. Used instead of the global wind (flt_12FBC48) while m_flags2 bit 0x2 is set
  /* 0x808 */ float m_windPitch;   // degrees, instead of flt_12FBC4C. sub_8450D0 turns the pair into a unit vector
  /* 0x80C */ float m_windStrength;   // times CHRDATA.m_f3C, instead of flt_12FBC44. All three set by sub_824C90 0x824C90 (ATEL Btl_282
                                     // 0x7A41D0, magic 0x816014), which also sets m_flags2 bit 0x2 through sub_824CC0
  /* 0x810 */ __int16 m_swaySettleRequest;   // one-shot. 1 from sub_824920 0x824920 (FFX_Ch_Allocate). Non-zero runs m_swaySettleIters
                                            // solver passes on the next sway pass, which clears it at 0x8245B8
  /* 0x812 */ __int16 m_swaySettleIters;   // by id in sub_824920: 10 for 1..4, 44, 45, 101..104, 122, 15 for 6 and 106, 20 for 7 and 107, else 1
  /* 0x814 */ float m_swayGainY;   // vertical response, used only while m_flags3 bit 0x1 is set. From the id table at 0xC495B0 in FFX_Ch_Allocate. Was m_f814
  /* 0x818 */ float m_swayGainXZ;   // horizontal response, the same table. Was m_f818
  /* 0x81C */ float m_swayBattleClamp;   // in battle caps the squared horizontal response when non-zero. 2.0 for ids 2, 3, 45, 102, 103, else 0 (sub_824920)
  /* 0x820 */ unsigned int m_flags3;   // mask 4: a snap motion change, set by FFX_Ch_SetMotionKey,
                                      // SetBlendFrames and SetHideFlagBits, cleared only by
                                      // FFX_Ch_StepSecondaryPhysics, forces the plain joint composer
                                      // (CHARACTER_MOTION.md 11, the old 'hidden' reading was wrong)
  /* 0x824 */ __int16 m_walkmeshTri;   // cached walkmesh triangle index, 0xFFFF forces a relocate. FFX_Ch_SetPos sets it to -1. The debug window prints it as "MapID num"
  unsigned __int8 _unk_826[0x2];
  /* 0x828 */ unsigned int m_groundAttrs;   // the current triangle's attribute dword, copied wholesale
                                           // from walkmeshTri+0x0C. Bits 0..6 surface type (the debug
                                           // window calls it "map", codes 48..63 indirect through
                                           // g_ffxGroundTypeRemap 0x13019D8), 7..8 enc, 9..10 eff
                                           // (footstep), 11..12 dic, 13..14 wat, 15..16 snd, then
                                           // 17..21 / 22..26 / 27..31 are a 5-bit SHADE WEIGHT for
                                           // triangle vertex 0 / 1 / 2. 0xFFFE0000 means off-mesh.
                                           // There is no map id in here - an earlier comment said the
                                           // low 17 bits were one, but that span is just the packed
                                           // subfields above, which the debug window prints as "id".
  unsigned __int8 _unk_82C[0x4];
  /* 0x830 */ void * m_instance;   // primary Phyre mesh-instance list
  /* 0x834 */ void * m_clonedClassChar;   // 560 byte cloth vertex clone record from FFX_Chr_CloneClassCharacterVertexData 0x63A810, not a ClassCharacter (CHR_CLUSTER.md 8). Built only when m_flags1 bit 0x200000 and m_hasClonedClassChar are set. Freed by FFX_Chr_FreeClonedClassCharacter 0x63A160
  /* 0x838 */ void * m_instance2;   // secondary Phyre mesh-instance list
  /* 0x83C */ int m_hasClonedClassChar;
  // 0x840..0x86F: bounds from the model's CDF data (FFX_ChCache_GetCdf), written by sub_826340 0x826340 from
  // FFX_Ch_OnInstanceReady when the cloned ClassCharacter is built. No reader found for any of them
  /* 0x840 */ float m_cdfBoundsMin[3];   // CDF floats 0..2 times 1000. FFX_Chr_BindClonedVertsToBones 0x827F0D reads [1]
  /* 0x84C */ float m_cdfBoundsMax[3];   // CDF floats 3..5 times 1000
  /* 0x858 */ float m_cdfBoundsInvSize[3];   // 1 / (max - min) per axis
  unsigned __int8 _unk_864[0xC];   // zeroed by sub_824620 0x824620 from FFX_Ch_Allocate, the middle float set to 11245.0 for id 101 and
                                   // rewritten by FFX_Chr_BindClonedVertsToBones 0x827F1E as m_cdfBoundsMin[1] minus a scaled joint
                                   // value. No reader found
  /* 0x870 */ unsigned __int8 m_jointComposerMode;   // initialised to 1
  /* 0x871 */ unsigned __int8 m_forceSmoothComposer;
  unsigned __int8 _unk_872[0x2];
  /* 0x874 */ void * m_pMeshPtrs;   // per-mesh pointer array, freed by FFX_Ch_Dispose
  /* 0x878 */ int m_updateStage;   // worker-thread stage marker: 1 queued, then 2, 3, 0 done
  /* 0x87C */ unsigned __int8 m_b87C;
  unsigned __int8 _unk_87D[0x3];
};

// BTLCHR   size=0x390 (912)
// The shared head of every battle record. FFX_Battle_GetActor 0x794020 hands out 93 records. 0..30 are
// BTLUNIT (0xF90), g_ffxBattleUnits 0x11334D4 (a pointer to a heap block) + 3984 * i. 31..92 are bare
// BTLCHR records, g_ffxBattleSideRecords 0x113446C (also a pointer) + 912 * (i - 31). Any other index gets
// the spare static BTLUNIT at 0x11334D8. The 62 bare records are NOT monsters: FFX_Btl_GetSideRecord 0x795B10 (unit, k)
// returns bare record unit + 31 * (k != 0), so GetActor index 31 + unit and 62 + unit are the two side
// records of unit 0..30, the weapon and armour models (equip entry +12 through sub_7ABDC0 0x7ABDC0, loaded
// by FFX_Btl_QueueWeaponChrLoads 0x7AD470, built by FFX_Btl_SetupWeaponChr 0x7938E0). Monsters are BTLUNITs 20..27.
// Sizes are pinned by the allocator FFX_Btl_AllocActorArrays 0x793530: 0x1E270 = 31 * 3984 and 0xDCE0 = 62 * 912, and it
// zeroes each record with sub_79F230(rec, 3984) and sub_79F230(rec, 912). The same functions (FFX_Btl_SetChrB183Request,
// FFX_Btl_SetFlag100Vote, sub_796480, Btl 294) run on a BTLUNIT and on its side records, so 0x000..0x04F is one layout.
// A side record only ever uses 0x000..0x047, the text at 0x050..0x38F is a monster BTLUNIT's.
struct BTLCHR {
  /* 0x000 */ CHR *m_chr;   // the record's CHR, null until FFX_Btl_OnUnitChrLoaded binds it. Read by about 80 functions, most Btl syscalls among them. AI property 87
  /* 0x004 */ int m_chrId;   // chr id FFX_Btl_QueueChrLoad loads. FFX_Btl_ResolveUnitChrId 0x79A440 derives it from m_typeId
                             // (41 for Rikku's alternate outfit). A weapon record gets the equipment model id, equip entry +12
                             // through sub_7ABDC0 0x7ABDC0, from FFX_Btl_SetupUnitRoster and FFX_Btl_SetupWeaponChr 0x7938E0
  /* 0x008 */ int m_baseChrId;   // the chr id before the Rikku outfit swap (FFX_Btl_ResolveUnitChrId). A monster gets param +116 here
                                 // (FFX_Btl_LoadUnitParams 0x79B783). A weapon record holds the second model FFX_Btl_QueueWeaponChrLoads 0x7AD470
                                 // queues when the weapon model is in the list at 0xC43984, else 0
  /* 0x00C */ unsigned __int8 m_unitIndex;   // own index 0..92, written by FFX_Btl_SetupUnitRoster. Picks the RNG stream in FFX_Btl_DamageFormula
  /* 0x00D */ unsigned __int8 m_attachKind;   // weapon records only: 14 for side record 0, 15 for side record 1 (FFX_Btl_SetupUnitRoster). sub_7821F0
                                              // 0x7821F0 hands it to the field ATEL actor at the end of a battle
  /* 0x00E */ unsigned __int16 m_typeId;   // type id: (id >> 12) 0 is a party or aeon index, 1 a monster 0x1000 | N. Weapon records get 0x5000 | unit
                                           // and 0x5100 | unit. FFX_Btl_SetupUnitRoster is the only writer, 0xFFFF is an empty enemy slot
  /* 0x010 */ unsigned __int8 m_exists;   // 1 when the unit takes part in this battle (FFX_Btl_SetupUnitRoster). Records 31..92 never set it
  /* 0x011 */ unsigned __int8 m_ownsDataFile;   // 1 by default. FFX_Btl_LoadMonsterData clears it when an earlier slot already loaded the same mNNN.bin,
                                                // and FFX_Btl_BindMonsterArchiveSections and sub_784000 0x784000 only bind sections for an owner
  /* 0x012 */ unsigned __int8 m_chrBorrowed;   // m_chr belongs to another record: FFX_Btl_SetupWeaponChr sets it on units 28..30 when it hands them the owner's
                                               // weapon CHR, and the dispose loops (sub_793480 0x793480, sub_7821F0) skip such records
  /* 0x013 */ unsigned __int8 m_weaponUnit;   // the unit 28..30 that stands in for this unit's weapon (FFX_Btl_SetupWeaponChr: Wakka 4 -> 28, Lulu 5 -> 29,
                                              // Yojimbo 14 -> 30), 0xFF none. FFX_Btl_SetUnitShowVote 0x797080 forwards visibility to it
  /* 0x014 */ unsigned __int8 m_primaryUnit;   // the first slot sharing this record's monster file (FFX_Btl_LoadMonsterData writes slot + 20), the
                                               // owner unit on units 28..30 (FFX_Btl_SetupWeaponChr). Target selector -22 is 1 << this (MONSTER_AI.md)
  /* 0x015 */ unsigned __int8 m_visible;   // 1 while all four m_showVotes are non-zero, recomputed by FFX_Btl_SetUnitShowVote 0x797080, which then shows or
                                           // hides the CHR and the weapon records
  /* 0x016 */ unsigned __int8 m_showVotes[4];   // four independent show requests, FFX_Btl_SetUnitShowVote(unit, slot, value). All 1 at roster setup. AI properties 84, 174, 216, 237
  /* 0x01A */ unsigned __int8 m_awaySummoning;   // 1 on the summoner when it leaves for an aeon, FFX_Btl_SwapUnitIntoPartySlot 0x7ADB8F (summon mode a3 > 0), 0 on return 0x7ADD77 (a3 < 0, from FFX_Btl_ReturnPartyAfterSummon). Set together with +0xDCB. No reader found [V]
                                                 // Written by Btl_SwapUnitIntoPartySlot 0x7ADAE0. No reader found
  /* 0x01B */ unsigned __int8 m_showAfterBattle;   // 1 at roster setup (FFX_Btl_SetupUnitRoster 0x79C1CC). At battle end sub_7821F0 0x78236F forces the field hide value to -1 when it is 0, so the CHR goes back to its field actor hidden (FFX_Ch_SetHideBit1). AI property 217 [V]
                                                   // 1 at roster setup. sub_7821F0 0x78236F forces the end of battle visibility value to -1 when it is 0. AI property 217
  /* 0x01C */ unsigned __int8 m_forceShowAfterBattle;   // sub_7821F0 0x782365 sets the field hide value to 0 when it is set, so the CHR is handed back to the field visible even outside the front three. m_showAfterBattle 0 still wins. 0 at roster setup (word write 0x79C1CC). AI property 336 [V]
                                                        // sub_7821F0 0x782365 drops the end of battle visibility value to 0 when it is set. AI property 336
  /* 0x01D */ unsigned __int8 m_showWeapons;   // the weapon records follow m_visible only while this is set (FFX_Btl_SetUnitShowVote). FFX_Btl_ResolveUnitChrId
                                               // clears it for Rikku's alternate outfit
  /* 0x01E */ unsigned __int8 m_startChrFxOnSetup;   // when set maybe_FFX_Btl_SetupUnitChr 0x7938A5 sets m_b1F and calls FFX_Ch_StartChrEffects. 1 for aeons (FFX_Btl_SetupUnitRoster 0x79C511) and when no entrance effect plays (FFX_Btl_QueueAeonTransition 0x7AE209), 0 on summon arrival 0x7ADD4C [V]
                                                     // 1 for aeons (FFX_Btl_SetupUnitRoster) and from FFX_Btl_QueueAeonTransition. Read by maybe_FFX_Btl_SetupUnitChr 0x7938A5
  /* 0x01F */ unsigned __int8 m_chrFxStarted;   // 1 right after FFX_Ch_StartChrEffects (maybe_FFX_Btl_SetupUnitChr 0x7938AC, FFX_Btl_SetUnitShowVote 0x7970FA), 0 with FFX_Ch_EndChrEffects (sub_788E60 0x788E7A). No reader found [V]
                                                // Written by FFX_Btl_SetUnitShowVote 0x797080, sub_788E60 0x788E60, maybe_Btl_SetupUnitChr 0x793670. No reader found
  /* 0x020 */ unsigned __int8 m_motionSet;   // motion set index: FFX_Ch_SetLocomotionMode and FFX_Ch_SetB185 (sub_796F70 0x796F70), FFX_Mot_SetByModeIndex
                                             // (Btl 047), FFX_Ch_MotionSetReadSync. 1 normally, 3 for units 0..17 in an underwater battle
  /* 0x021 */ unsigned __int8 m_disposeMotionSetAtEnd;   // 1 at setup (word 257, FFX_Btl_SetupUnitRoster 0x79C16D). sub_793480 0x7934C8 turns it into bit 4 for sub_796DC0, which then disposes the motion set (FFX_Ch_MotionSetDispose on +8). sub_7821F0 0x7824B6 clears it when the field actor keeps it. AI property 238 [V]
                                                         // 1 at setup. sub_793480 0x7934C8 passes it as bit 4 to sub_796DC0 at battle end. AI property 238
  /* 0x022 */ unsigned __int8 m_disposeChrDataAtEnd;   // 1 at setup. sub_793480 0x7934BE turns it into bit 2 for sub_796DC0, which then calls FFX_Ch_DataDispose on m_chrId. sub_7821F0 0x782420 and 0x78247A clear it once the CHR is handed to the field actor. AI property 239 [V]
                                                       // sub_793480 0x7934BE passes it as bit 2 to sub_796DC0 at battle end. AI property 239
  /* 0x023 */ unsigned __int8 m_b23;   // AI property 222. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0
  /* 0x024 */ unsigned __int8 m_loadChr;   // FFX_Btl_StepSubPhase and FFX_Btl_OnUnitChrLoaded only load a CHR for a record with m_typeId != 0xFFFF
                                           // and this set. 1 at setup. AI property 88
  /* 0x025 */ unsigned __int8 m_b183Index;   // FFX_Btl_SetChrB183Request 0x797020: the m_b183Requests entry that won
  /* 0x026 */ char m_b183Requests[3];   // three requests, each -1 or an index into m_b183Table. FFX_Btl_SetChrB183Request(rec, slot, value) keeps the last
                                        // non-negative one, stopping at a 0. AI property 241
  /* 0x029 */ unsigned __int8 m_b183Value;   // m_b183Table[m_b183Index], pushed to CHR.m_b183 with FFX_Ch_SetB183 (FFX_Btl_SetChrB183Request)
  /* 0x02A */ unsigned __int8 m_b183Table[3];   // {0, 1, 3} at roster setup
  /* 0x02D */ unsigned __int8 m_flag100;   // 1 while all four m_flag100Votes are set, pushed to FFX_Ch_SetFlags1Bit100Inv by FFX_Btl_SetFlag100Vote 0x796F20. AI properties 127, 332
  /* 0x02E */ unsigned __int8 m_flag100Votes[4];   // four requests for m_flag100, FFX_Btl_SetFlag100Vote(rec, slot, value). All 1 at roster setup
  /* 0x032 */ unsigned __int8 m_chrFlag80Inv;   // pushed to FFX_Ch_SetFlags1Bit80Inv when the CHR is shown (FFX_Btl_SetUnitShowVote, sub_796F70)
  /* 0x033 */ unsigned __int8 m_chrFlag1;   // pushed to FFX_Ch_SetFlags1Bit1 when the CHR is shown (FFX_Btl_SetUnitShowVote, sub_793E00 0x793E00)
  /* 0x034 */ unsigned __int8 m_groundMode;   // CHR ground mode, FFX_Ch_SetGroundMode (sub_796F70, FFX_Btl_SetUnitGroundMode). 1 on land, 0 in an
                                              // underwater battle. AI property 244
  /* 0x035 */ unsigned __int8 m_hasDataSec2;   // set by sub_788B70 0x788B70 once sub_784000 bound m_dataSec2, read by sub_788B30 0x788B30
  /* 0x036 */ unsigned __int8 m_pendingShowFx;   // set by Btl 093. FFX_Btl_SetUnitShowVote clears it and calls sub_7FB290 on the CHR the next time it is shown
  /* 0x037 */ unsigned __int8 m_chrB181;   // pushed to FFX_Ch_SetUpdateWhileHidden by sub_796F70. 1 at roster setup
  /* 0x038 */ unsigned __int8 m_chrReady;   // 1 once the CHR is set up (FFX_Btl_OnUnitChrLoaded, FFX_Btl_OnUnitChrReloaded, FFX_Btl_SetupWeaponChr). The motion
                                            // and placement passes (sub_7882C0 0x7882C0, sub_78AC20 0x78AC20) skip a record without it
  /* 0x039 */ unsigned __int8 m_useEnemyCommonEvents;   // 1 for every enemy at roster setup (0x79C5A7). sub_7AB360 0x7AB3F0, an enemy unit event, pushes the shared fallback event group 4 instead of group 3 (the party one) when it is 1. Group meaning is [I], the switch is [V]. AI property 339
                                                        // AI property 339. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7AB360 0x7AB360
  /* 0x03A */ unsigned __int8 m_modelPacketPurged;   // 1 after FFX_Ch_ModelPurgePacket (sub_796DC0 0x796DE2, sub_7AD750 0x7AD77A, both on flag bit 8). sub_793D90 recreates the packet with FFX_Ch_ModelCreatePacket when it is set and clears it [V]
                                                     // Written by sub_793D90 0x793D90, sub_7AD750 0x7AD750, sub_796DC0 0x796DC0. Read by sub_793D90 0x793D90
  /* 0x03B */ unsigned __int8 m_innateAutoLife;   // sub_78DEB0 0x78DEB6 sets Auto-Life (status 2 bit 0x200) when it is set. Called on load (FFX_Btl_LoadUnitParams), reentry (FFX_Btl_ResetUnitForReentry) and revival (sub_78E150). Only AI property 334 writes it (0x7B6658) [V]
                                                  // AI property 334. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_78DEB0 0x78DEB0
  /* 0x03C */ unsigned __int8 m_b3C;   // sub_7969C0 0x7969C0 passes (m_b3C || m_b3D) to sub_835780 on the CHR
  /* 0x03D */ unsigned __int8 m_b3D;   // see m_b3C. Set by FFX_Btl_SwapUnitIntoPartySlot, read by sub_795C40 0x795C40
  /* 0x03E */ unsigned __int8 m_keepChrResourcesAtEnd;   // Btl 294 sets it on the unit and both side records (0x7A857A, 0x7A858B). sub_796DC0 0x796E28 and 0x796E49 then skip the motion set and CHR data disposal at battle end. sub_793480 0x7934BB clears it [V]
                                                         // set by Btl 294 on a unit and its two weapon records, cleared by sub_793480. sub_796DC0 0x796E28 reads it
  unsigned __int8 _unk_3F[0x1];
  /* 0x040 */ float m_alpha;   // CHR shade alpha, FFX_Ch_SetShadeAlpha (sub_796480 0x796480, magic host API 541). 1.0 at setup
  /* 0x044 */ float m_f44;   // 1.0 at roster setup, read by sub_793E00 0x793E5E
  /* 0x048 */ void *m_dataFile;   // the loaded data file. For a monster the mNNN.bin image FFX_Btl_LoadMonsterData loads, shared between
                                  // slots of the same type. FFX_Btl_BindMonsterArchiveSections reads its section table
  /* 0x04C */ void *m_dataSec2;   // m_dataFile + header dword [2] when the file has more than 2 sections, else 0 (sub_784000 0x784000)
  /* 0x050 */ char m_name[64];   // monster name, raw FFX glyph text from monster1/2/3.bin, or the 'A', 'B' suffixed name
                                 // (FFX_Btl_LoadMonsterData). FFX_Btl_LoadUnitParams copies 40 bytes of it into m_battleName
  /* 0x090 */ char m_description[256];   // monster description text (FFX_Btl_LoadMonsterData). No reader found in the IDB
  /* 0x190 */ char m_scanText[512];   // the third monster text, the Sensor and Scan text (FFX_Btl_LoadMonsterData). No reader found in the IDB
};


// BTLHIT   size=0x2C (44)
// One staged hit inside BTLHITLOG, written by FFX_Btl_ComputeEffectForTarget 0x78E630 and applied by
// FFX_Btl_ApplyPendingEffects 0x78F060.
struct BTLHIT {
  /* 0x000 */ unsigned __int8 m_reaction;   // reaction motion kind handed to sub_78CA20, 5, 6 and 8 get a random variant
  /* 0x001 */ unsigned __int8 m_numberKind;   // floating number kind, 1 or 2 (FFX_Btl_ShowFloatingNumber)
  /* 0x002 */ unsigned __int8 m_numberArg;   // argument for that number
  /* 0x003 */ unsigned __int8 m_b03;   // passed to sub_7B12C0 with m_b04
  /* 0x004 */ unsigned __int8 m_b04;   // passed to sub_7B12C0
  unsigned __int8 _unk_5[0x1];
  /* 0x006 */ unsigned __int8 m_b06;   // passed to sub_78D270 after the status copy
  /* 0x007 */ unsigned __int8 m_statusCount[13];   // the target's new status counters
  /* 0x014 */ unsigned __int16 m_status1;   // the target's new status 1 word
  /* 0x016 */ unsigned __int16 m_status2;   // the target's new status 2 word
  /* 0x018 */ unsigned __int16 m_applyMask;   // bit 0 HP, bit 1 MP, bit 2 CTB, selects which m_amount entries apply
  /* 0x01A */ unsigned __int16 m_w1A;   // read by sub_79A920 0x79A9F9
  /* 0x01C */ int m_i1C;   // passed to FFX_Btl_GainOverdriveOnHit
  /* 0x020 */ int m_amount[3];   // HP, MP and CTB amounts (FFX_Btl_ApplyHpDamage, FFX_Btl_ApplyMpDamage, FFX_Btl_ApplyCtbDelay), summed by
                                 // sub_79A760 0x79A760
};


// BTLHITLOG   size=0x2D8 (728)
// A pending effect block, BTLUNIT.m_hitLog[2]. A 24 byte header and 16 staged hits.
struct BTLHITLOG {
  /* 0x000 */ unsigned __int8 m_next;   // next entry to apply, counted up by FFX_Btl_ApplyPendingEffects
  /* 0x001 */ unsigned __int8 m_count;   // staged entries
  /* 0x002 */ unsigned __int8 m_attacker;   // unit whose action staged the block, 0xFF free
  /* 0x003 */ unsigned __int8 m_commandSlot;   // command slot matched with the attacker
  /* 0x004 */ unsigned __int8 m_rewardFlags;   // bit 1 moves the attacker's overdrive gauge to the target, bit 2 teaches the Ronso Rage of m_rageUnit
  /* 0x005 */ unsigned __int8 m_state;   // bit 1 something applied, bit 2 the AI reaction ran, bit 4 a reaction was picked
  /* 0x006 */ unsigned __int8 m_odKind;   // passed to FFX_Btl_GainOverdriveOnHit
  /* 0x007 */ unsigned __int8 m_stealPending;   // a stolen item waits to be shown
  /* 0x008 */ __int16 m_stealCount;   // stolen item count
  /* 0x00A */ unsigned __int16 m_stealItem;   // stolen item id
  /* 0x00C */ unsigned __int8 m_gilPending;   // a gil change waits to be applied
  unsigned __int8 _unk_D[0x3];
  /* 0x010 */ int m_gil;   // gil gained, FFX_SaveData_SpendGil(-m_gil)
  unsigned __int8 _unk_14[0x3];
  /* 0x017 */ unsigned __int8 m_rageUnit;   // unit whose reward record holds the Ronso Rage to teach
  /* 0x018 */ BTLHIT m_hits[16];   // the staged hits
};


// BTLUNIT   size=0xF90 (3984)
// The battle unit record, units 0..30: 0..6 the characters, 8..17 the aeons, 20..27 the eight enemy slots
// (g_ffxBattleEnemyUnits 0x1134468 holds &unit[20], FFX_Btl_GetEnemyUnit 0x795AA0), 28..30 the stand-ins for
// Wakka's ball, Lulu's doll and Daigoro (FFX_Btl_GetWeaponStandInUnit 0x795BB0). FFX_Btl_SetupUnitRoster 0x79C110 writes the
// defaults, FFX_Btl_LoadUnitParams 0x79B4F0 zeroes and fills the 492 byte stat block at 0x540..0x72B.
//
// Field naming pass 2026-10-13 (BATTLE_ACTOR.md). Every field below was found by a Hex-Rays walk that proves
// the base came from FFX_Battle_GetActor, FFX_Btl_GetEnemyUnit, FFX_Btl_GetSideRecord, FFX_Btl_GetWeaponStandInUnit, a function returning
// one of them, or a parameter every caller passes such a pointer in. "No reader found" means none in that
// walk. The magic DLLs get unit pointers through the host API table and are not covered.
// 2026-10-28 (BATTLE_ACTOR.md 9): units 18 and 19 are never filled, every character loop stops at 18 and the
// enemy slots start at 20. The magic DLLs are now walked: through a raw pointer they only write +0x614 (four DLLs,
// FFX_Battle_GetActor slot 925) and read the target lists at +0xD40, +0xD60 and +0xD80, everything else goes
// through exe functions that take a unit index and are in the walk. Fields named in that pass cite their
// reader or writer first, the first pass's access list follows on the next comment line.
// m_b/m_w/m_i/m_f names are fields at a confirmed offset and size whose meaning is not known yet, their
// comment lists the first writers and readers. Script stat names (stat_*, motion_*) come from the macro
// writer SG_DebugWin_WriteMotionMacro 0x7DCF70. "AI property N" is the FFX_BtlAi_GetProperty 0x7B2DC0 or
// FFX_BtlAi_SetProperty 0x7B4B70 case that reads or writes the field.
struct BTLUNIT {
  /* 0x000 */ BTLCHR m_head;   // the shared head, see BTLCHR. AI properties 84, 87..88, 127, 174, 216..217, 222, 237..239, 241, 244, 332, 334, 336, 339
  /* 0x390 */ float m_moveStartPos[3];   // start position of a scripted move, written by FFX_Btl_StartUnitMove 0x7AB540 (magic host API 701) and
                                         // sub_7B7C80 0x7B7C80. [1] is the base height of the jump arc in FFX_Btl_StepUnitMove 0x7AA2A0
  /* 0x39C */ int m_motionEventBits;   // bit k set by FFX_Btl_OnMotionEvent for motion event k, -1 from sub_7AB360, 0 from sub_793EF0.
                                       // sub_795660 0x795660 returns it
  /* 0x3A0 */ float m_scale[3];   // unit scale, 1.0 at setup. Btl 040 writes all three and FFX_Ch_SetScaleMasked. maybe_FFX_Btl_GetUnitHeight
                                  // multiplies the CHR height by [1]. FFX_Btl_GetUnitScale 0x795720 returns these three, it is misnamed. AI properties 270..272
  /* 0x3AC */ float m_attachScale;   // 1.0 at setup, Btl 191 writes it, maybe_FFX_Btl_SetupUnitChr passes it to FFX_Ch_SetAttachScale
  /* 0x3B0 */ float m_lookAt[4];   // look-at point, sub_7AF920 0x7AF920 builds it with FFX_Came_ApplyShake between the target points of
                                   // units m_b6D3 and m_b6D4, then turns it into m_homeFacing[3] with FFX_Came_VecToPolar
  /* 0x3C0 */ float m_savedPos[4];   // unit position snapshot, sub_78F040 0x78F040 fills it with maybe_FFX_Btl_GetUnitPos and sets m_savedPosValid,
                                     // sub_7A93E0 0x7A93E0 reads it back
  unsigned __int8 _unk_3D0[0x10];
  /* 0x3E0 */ float m_trackPos[4];   // unit position, sub_7A9710 0x7A9710 fills it with maybe_FFX_Btl_GetUnitPos and measures from it with
                                     // FFX_Came_VecToPolar
  /* 0x3F0 */ unsigned __int8 m_motionId;   // the motion currently played: FFX_Btl_PlayUnitMotion 0x7883BF and sub_7A8FC0 0x7A90C5 store it, 0xFF for none (FFX_Btl_SetupUnitRoster, maybe_FFX_Btl_HoldUnitMotion). FFX_Btl_GetUnitMotionId 0x7A9A30 returns it, Btl 088 compares it [V]
                                            // Written by Btl_OnUnitChrReloaded 0x783BC0, Btl_SetupUnitRoster 0x79C110, sub_7B7D70 0x7B7D70 and 4 more. Read by sub_7AF320 0x7AF320, sub_78AC20 0x78AC20, sub_787840 0x787840 and 8 more
  /* 0x3F1 */ unsigned __int8 m_motionKind1Id;   // the motion id script motion kind 1 maps to in sub_7A8C80 0x7A8CB5 (Btl 007, 111, 163). 16 at roster setup (0x79C204), Btl 031 writes it (0x7A5FB0) [V]
                                                 // Written by Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_031_resi 0x7A5F80. Read by sub_7A8C80 0x7A8C80
  /* 0x3F2 */ unsigned __int8 m_queuedEventId;   // unit battle event waiting to start, 0xFF none. sub_7AA160 0x7AA185 runs it through sub_7AB360 once the unit is not busy (+0x432 == 0) and resets it to 0xFF. Btl 022 queues it (0x7A2A64), sub_787D70 and FFX_Btl_StepMagicEffectStates cancel it [V]
                                                 // Written by sub_788480 0x788480, Btl_SetupUnitRoster 0x79C110, sub_7AA160 0x7AA160 and 3 more. Read by sub_7AA160 0x7AA160
  /* 0x3F3 */ unsigned __int8 m_motionLocked;   // 1 while an effect or a forced motion owns the CHR: FFX_Btl_PlayUnitMotion 0x78831F, maybe_FFX_Btl_HoldUnitMotion, Btl 163. sub_7A8FC0 0x7A9031 and Btl 022 refuse script motions while set, Btl 008 waits. sub_788270 clears it (word write) [V]
                                                // Written by sub_7882C0 0x7882C0, AtelSys_Btl_163_resi 0x7A3730, sub_7883F0 0x7883F0 and 1 more. Read by AtelSys_Btl_008_poll 0x7A3980, AtelSys_Btl_022_resi 0x7A2A20, sub_7A8FC0 0x7A8FC0 and 2 more
  /* 0x3F4 */ unsigned __int8 m_defeatFinished;   // 1 when the defeat sequence ends, sub_78CCA0 0x78CCB9 (from sub_78DC30 and Btl 057). FFX_Btl_ResetUnitForReentry 0x78D5D9 clears it. sub_795A60 0x795A80 returns it for ally slots [V]
                                                  // Written by sub_78CCA0 0x78CCA0, Btl_ResetUnitForReentry 0x78D530. Read by sub_795A60 0x795A60
  /* 0x3F5 */ unsigned __int8 m_leaveOnDeath;   // 3 for enemies, 0 otherwise (FFX_Btl_SetupUnitRoster). When non-zero FFX_Btl_OnUnitDefeated clears
                                                // m_onField on death, so a dead enemy leaves the field and a dead character stays (BATTLE_START_AND_END.md 2.4).
                                                // FFX_Btl_ResetUnitForReentry restores it from m_leaveOnDeathDefault. AI property 79
  /* 0x3F6 */ unsigned __int8 m_leaveOnDeathDefault;   // the value FFX_Btl_ResetUnitForReentry copies back into m_leaveOnDeath. Btl 078 writes both
  /* 0x3F7 */ unsigned __int8 m_deathEffectStarted;   // 1 when sub_79E3D0 0x79E48C launches a defeat dissolve effect. sub_78CCA0 0x78CCA6 then leaves +0x436 set instead of clearing it. Never cleared in the walk [V]
                                                      // Written by sub_79E3D0 0x79E3D0. Read by sub_78CCA0 0x78CCA0
  /* 0x3F8 */ unsigned __int8 m_centreSlot;   // 1 for party slot 1 and aeon 16 at setup (0x79C3B0, 0x79C528), passed on by a switch (0x7ADD18). sub_796D70 0x796D90 treats a unit with it as the centre of a 3+ unit target mask, so sub_7969F0 does not re-aim it. Debug label C. AI property 297 [V]
                                              // AI property 297. Written by Btl_SetupUnitRoster 0x79C110, Btl_SwapUnitIntoPartySlot 0x7ADAE0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_796D70 0x796D70, sub_7DBEE0 0x7DBEE0
  /* 0x3F9 */ unsigned __int8 m_returnsHome;   // 1 for aeons (0x79C515). sub_7AAAE0 0x7AAB15 keeps the away from home check (+0x508) running for it off the front row, sub_78C4C0 0x78C4EB holds its turn while away, KO departure gets prio 64 (0x7AE27D). Debug label RT. AI property 330 [I]
                                               // 1 for the aeon units 8..17 at roster setup. Read by FFX_Btl_QueueAeonTransition and Btl 279. AI property 330
  /* 0x3FA */ unsigned __int8 m_pendingForceAttackReturn;   // FFX_Btl_StartCommandExec 0x7AEC86 sets it to 1 for ability kind 30 around the attack event start and 0x7AECC4 clears it. sub_7AB360 0x7AB39B copies it into m_b3FB at every unit event start [V]
                                                            // Written by Btl_StartCommandExec 0x7AE370. Read by sub_7AB360 0x7AB360
  /* 0x3FB */ unsigned __int8 m_forceAttackReturn;   // latched from m_b3FA at event start (sub_7AB360 0x7AB3A1). sub_7B4A70 0x7B4A76 ORs motion_type 0x8 attack_return into m_motionType while set, sub_7A88E0 doubles unit 29's run speed. AI property 340 [V]
                                                     // AI property 340. Written by sub_7AB360 0x7AB360, BtlAi_SetProperty 0x7B4B70. Read by sub_7A88E0 0x7A88E0, BtlAi_GetProperty 0x7B2DC0, sub_7B4A70 0x7B4A70
  /* 0x3FC */ unsigned __int16 m_motionSpeed;   // motion speed factor, sub_78AC20 0x78ACCB and sub_7A8FC0 scale frame counts by it / 8. 256 at setup,
                                                // Btl 058 writes it, sub_7882C0 hands it to sub_788430
  unsigned __int8 _unk_3FE[0x2];
  /* 0x400 */ unsigned __int16 m_appliedMotionSpeed;   // the speed sub_78AC20 last pushed with FFX_Ch_SetSpeedMulA (0x78AD2A). 256 at setup, 0xFFFF forces a re-push (FFX_Btl_OnUnitChrReloaded, Btl 181). FFX_Btl_GetUnitAppliedMotionSpeed 0x7956C0 returns it. Debug SP. AI property 251 [V]
                                                       // AI property 251. Written by Btl_OnUnitChrReloaded 0x783BC0, sub_78AC20 0x78AC20, Btl_SetupUnitRoster 0x79C110 and 1 more. Read by BtlAi_GetProperty 0x7B2DC0, sub_78AC20 0x78AC20, sub_787840 0x787840 and 5 more
  /* 0x402 */ unsigned __int16 m_motionSpeedOverride;   // speed used instead of m_motionSpeed * m_statusMotionSpeed / 8 while m_motionSpeedOverrideOn is 1 (sub_78AC20 0x78ACC2, sub_7A8FC0 0x7A90EE). FFX_Btl_SetUnitMotionSpeedOverride 0x78845A writes it, sub_7AA260 and sub_7AA280 reset it to 256 [V]
                                                        // Written by sub_788430 0x788430, sub_7AA260 0x7AA260, sub_7AA280 0x7AA280. Read by sub_78AC20 0x78AC20, sub_7A8FC0 0x7A8FC0
  /* 0x404 */ unsigned __int8 m_motionSpeedOverrideOn;   // 1 selects m_motionSpeedOverride (sub_78AC20 0x78ACB3). FFX_Btl_SetUnitMotionSpeedOverride 0x788461 sets it, maybe_FFX_Btl_ResetUnitsForEffectStart and sub_7AB6C0 clear it. FFX_Btl_PlayUnitMotion 0x78830E installs the base speed when it is 0 [V]
                                                         // Written by sub_788430 0x788430, sub_793E00 0x793E00, sub_788E90 0x788E90 and 3 more. Read by sub_78AC20 0x78AC20, sub_7882C0 0x7882C0, sub_7A8FC0 0x7A8FC0
  /* 0x405 */ unsigned __int8 m_fixedSpeedNextMotion;   // set when an effect ends (FFX_Btl_StepMagicEffectStates 0x78882A). The next script motion (sub_7A8FC0 0x7A90A8) clears it and calls sub_7AA280, so it plays at a fixed 256 ignoring Haste and Slow. maybe_FFX_Btl_EffectReturnToIdle clears it. AI property 252 [V]
                                                        // AI property 252. Written by sub_787C30 0x787C30, sub_788480 0x788480, sub_7A8FC0 0x7A8FC0 and 2 more. Read by BtlAi_GetProperty 0x7B2DC0, sub_7A8FC0 0x7A8FC0
  /* 0x406 */ unsigned __int8 m_statusMotionSpeed;   // status speed factor in eighths, sub_78AC20 0x78ACBA: 8 normal, 16 Haste, 4 Slow on motions 16..42, 0 when petrified or idle under Threaten, an attached record copies its owner's. Scales m_motionSpeed (sub_7A8FC0 0x7A90F7). Debug WT [V]
                                                     // Written by sub_78AC20 0x78AC20, Btl_SetupUnitRoster 0x79C110. Read by sub_78AC20 0x78AC20, sub_7A8FC0 0x7A8FC0, sub_7DBEE0 0x7DBEE0
  /* 0x407 */ unsigned __int8 m_visibleAtPlacement;   // FFX_Btl_PlaceMonstersAtHome 0x784486 hides the monster (FFX_Btl_SetUnitShowVote1 0) when it is 0. Btl 265 SetAirborneMask sets it to 1 (0x7A3E69) so the monster stays shown [V]
                                                      // Written by AtelSys_Btl_265_resi 0x7A3DF0. Read by sub_784420 0x784420
  /* 0x408 */ unsigned __int8 m_motionVariant;   // picks the alternate motion set for script motion kinds 2..5 in sub_7A8C80 0x7A8CC7 (1..5 give 176/184/192/200 style ids, else 88..91). FFX_Btl_StartCommandExec clears it before the attack or escape event. AI property 229 [V]
                                                 // AI property 229. Written by Btl_SetupUnitRoster 0x79C110, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by sub_7A8C80 0x7A8C80, BtlAi_GetProperty 0x7B2DC0
  /* 0x409 */ unsigned __int8 m_motionVariantAlt;   // the same variant switch as m_motionVariant but for script motion kinds 12 and 13 (sub_7A8C80 0x7A8C95). Only AI property 269 writes it (0x7B61F0) [V]
                                                    // AI property 269. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7A8C80 0x7A8C80
  /* 0x40A */ unsigned __int8 m_flaggedMotionPlaying;   // 1 when the current motion came from Btl 111 PlayMotionFlagged (sub_7A8FC0 0x7A90CB, a4), 0 from FFX_Btl_PlayUnitMotion 0x788334 and Btl 007. With m_b40B set the issuer's command waits in state 2 (sub_792880, FFX_Btl_ExecCommand). AI property 123 [V]
                                                        // AI property 123. Written by sub_7882C0 0x7882C0, sub_7A8FC0 0x7A8FC0, BtlAi_SetProperty 0x7B4B70. Read by sub_792880 0x792880, BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0
  /* 0x40B */ unsigned __int8 m_b40B;   // No writer found. Read by sub_792880 0x792880
  /* 0x40C */ unsigned __int8 m_attackNormalFrame;   // script stat stat_attack_normal_frame (SG_DebugWin_WriteMotionMacro 0x7DCF70 writes the macro, the CHR data 2
                                                     // debug window edits it). AI property 124
  /* 0x40D */ unsigned __int8 m_attackNearFrame;   // script stat stat_attack_near_frame (SG_DebugWin_WriteMotionMacro). AI property 278
  /* 0x40E */ unsigned __int8 m_attackMotionFrame;   // script stat stat_attack_motion_frame (SG_DebugWin_WriteMotionMacro). AI property 291
  /* 0x40F */ unsigned __int8 m_soundKind6Enabled;   // sub_79FE80 0x79FEC1, the unit sound picker, returns no sound for kind 6 while it is 0. 1 at setup and after each action (sub_793E00), FFX_Btl_SetUnitSoundKind6Flag 0x79FD33 writes it [V]
                                                     // Written by Btl_SetupUnitRoster 0x79C110, sub_79FD20 0x79FD20, sub_793E00 0x793E00. Read by sub_79FE80 0x79FE80
  /* 0x410 */ unsigned __int8 m_soundVariantCycle;   // round robin counter in sub_79FE80 0x79FF00: a sound entry with 0x100 picks variant counter % 4, with 0x200 counter % 2, then stores the next value [V]
                                                     // Written by sub_79FE80 0x79FE80. Read by sub_79FE80 0x79FE80
  /* 0x411 */ unsigned __int8 m_coveringAlly;   // sub_7AF920 0x7AF96D sets it when the unit steps in front of its guarded ally (event 60 or 61). Cleared at action end (sub_793E00 word write) and by Btl 269. No reader found [V]
                                                // Written by AtelSys_Btl_269_resi 0x7A3C20, sub_7AF920 0x7AF920, sub_793E00 0x793E00. No reader found
  /* 0x412 */ unsigned __int8 m_idleHold;   // sub_7AA160 0x7AA1B9 only runs the home return logic while it is 0. maybe_FFX_Btl_ResetUnitsForEffectStart 0x788EB0 sets it on all units from the actor's m_effectHoldsUnits, maybe_FFX_Btl_SetUnitMotionHold writes it. Btl 048 reads it. AI property 93 [V]
                                            // AI property 93. Written by maybe_Btl_SetUnitFlag1042 0x796110, sub_788E90 0x788E90, BtlAi_SetProperty 0x7B4B70. Read by AtelSys_Btl_048_resi 0x7A5090, sub_7AA160 0x7AA160, BtlAi_GetProperty 0x7B2DC0 and 1 more
  /* 0x413 */ unsigned __int8 m_effectHoldsUnits;   // FFX_Btl_StartCommandExec 0x7AEC1B sets it (0 for ability kinds 23 and 24). When the effect starts it becomes every unit's m_idleHold (FFX_Btl_StepMagicEffectStates 0x788715). When 0 FFX_Btl_StartEffectReactions sets m_b414 [V]
                                                    // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788480 0x788480, maybe_Btl_EffectSetReady 0x788200, sub_7DBEE0 0x7DBEE0 and 1 more
  /* 0x414 */ unsigned __int8 m_presetMotionEventBits;   // set by FFX_Btl_StartEffectReactions 0x788FFA when m_effectHoldsUnits is 0. sub_7AB360 0x7AB380 then sets m_motionEventBits to -1 at each event start. Cleared at action end by sub_793E00 [V]
                                                         // Written by sub_793E00 0x793E00, sub_788F90 0x788F90. Read by sub_7AB360 0x7AB360, sub_7DBEE0 0x7DBEE0
  /* 0x415 */ unsigned __int8 m_moveKind;   // scripted move kind, 0 idle. FFX_Btl_StartUnitMove sets it from its argument & 0x3F, sub_7AB010 0x7AB010 sets 7.
                                            // FFX_Btl_StepUnitMove 0x7AA2A0 steps it: 7 follows a spline from 0x1136428, 16 orbits in polar form, bit 0x80
                                            // re-aims every frame. Cleared with m_moveActive when m_moveFrame reaches m_moveTotalFrames. AI property 331
  /* 0x416 */ unsigned __int8 m_moveArg;   // FFX_Btl_StartUnitMove argument 3, passed back unchanged when FFX_Btl_StepUnitMove restarts the move. AI property 90
  /* 0x417 */ unsigned __int8 m_b417;   // Written by sub_7AB010 0x7AB010, AtelSys_Btl_187_resi 0x7A4CF0, FFX_Btl_StartUnitMove 0x7AB540 and 1 more. No reader found
  /* 0x418 */ unsigned __int8 m_moveActive;   // cleared with m_moveKind when a move ends (FFX_Btl_StepUnitMove)
  /* 0x419 */ unsigned __int8 m_moveUseGravity;   // 0xFF when FFX_Btl_StartUnitMove got a non-zero gravity, then FFX_Btl_StepUnitMove adds the parabolic arc
  /* 0x41A */ unsigned __int8 m_petrifyReactionPlayed;   // FFX_Btl_PlayUnitReaction 0x78CBEF sets it when a petrified unit plays event 56. While set an unknown reaction kind plays event 0 (0x78CB91). Cleared after any normal reaction (0x78CBE2) [V]
                                                         // Written by sub_78CA20 0x78CA20. Read by sub_78CA20 0x78CA20
  /* 0x41B */ unsigned __int8 m_directionFixFlag;   // script stat stat_direction_fix_flag (SG_DebugWin_WriteMotionMacro), the CHR data 1 debug window labels it "DIR : Fix". AI property 164
  /* 0x41C */ unsigned __int8 m_idleReturnPending;   // sub_791780 0x791786 runs event 0 for the unit when it and m_directionChangeEffect are set and it is not busy or away. FFX_Btl_EndPhaseStep 0x791B08 waits on it. Set on reload, reentry, poison, aeon arrival. AI property 162. Debug D [V]
                                                     // AI property 162. Written by sub_793CF0 0x793CF0, sub_787950 0x787950, sub_7B7D70 0x7B7D70 and 12 more. Read by BtlAi_GetProperty 0x7B2DC0, Btl_EndPhaseStep 0x7917D0, sub_7DBEE0 0x7DBEE0 and 1 more
  /* 0x41D */ unsigned __int8 m_directionChangeEffect;   // script stat stat_direction_change_effect (SG_DebugWin_WriteMotionMacro), debug label "DIR : Return". 1 at setup. AI property 163
  /* 0x41E */ unsigned __int8 m_b41E;   // AI property 125. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_794DB0 0x794DB0, BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0
  /* 0x41F */ unsigned __int8 m_b41F;   // AI property 126. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_794D70 0x794D70, BtlAi_GetProperty 0x7B2DC0
  /* 0x420 */ unsigned __int8 m_moveArcMode;   // arc mode 0..3 FFX_Btl_BuildArcPath 0x7B7945 switches on, written by Btl 028 (0x7A5A00), FFX_Btl_SetUnitMoveArcMode 0x7960F6, set to 3 by BuildArcPath. Getters read it signed. AI property 91 [V]
                                               // AI property 91. Written by sub_7B78C0 0x7B78C0, maybe_Btl_SetUnitFlag1056 0x7960D0, AtelSys_Btl_028_resi 0x7A59D0 and 2 more. Read by sub_7B78C0 0x7B78C0, BtlAi_GetProperty 0x7B2DC0, sub_795640 0x795640 and 1 more
  unsigned __int8 _unk_421[0x1];
  /* 0x422 */ unsigned __int8 m_linkUnit;   // own index at roster setup, AI property 89 points it at a target selector result. FFX_Btl_ResolveTargets
                                            // 0x792062 and sub_78CA20 0x78CA33 read it, Btl 215 tests its KO
  /* 0x423 */ unsigned __int8 m_fadeInPending;   // bit 0 set by maybe_FFX_Btl_SetupUnitChr 0x79387B when the unit starts hidden. sub_796550 (from FFX_Btl_OnUnitChrLoaded 0x783AA2 and Btl 148) fades the CHR in and shows it only when bit 0 is set, then clears it (0x796588). AI property 169 [V]
                                                 // AI property 169. Written by maybe_Btl_SetupUnitChr 0x793670, sub_796550 0x796550, BtlAi_SetProperty 0x7B4B70. Read by maybe_Btl_SetupUnitChr 0x793670, Btl_OnUnitChrLoaded 0x7839B0, BtlAi_GetProperty 0x7B2DC0 and 1 more
  /* 0x424 */ unsigned __int8 m_sameTypeRandomRank;   // sub_7842F0 0x7843D5 gives each monster sharing a data file a distinct value 8 * k + (rand & 3), k a random rank in the group, drawn on the unit RNG streams. Only AI property 233 reads it [V]
                                                      // AI property 233. Written by sub_7842F0 0x7842F0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0
  /* 0x425 */ unsigned __int8 m_savedPosValid;   // set by sub_78F040 0x78F040 with m_savedPos, cleared by sub_793E00 and sub_78D620. Btl 157 waits on it. AI property 171
  /* 0x426 */ unsigned __int8 m_holdPosition;   // when set sub_78D680 0x78D6A4 does not snap the unit position to its CHR bone 6 and sub_7AAAE0 0x7AAB5D skips the away from home check. AI property 118, debug label P [V]
                                                // AI property 118. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0, sub_78D680 0x78D680 and 1 more
  /* 0x427 */ unsigned __int8 m_rapidReturnCount;   // sub_7AA160 0x7AA1FA counts home return events started less than 2 frames after the last one, at 4 it resets and clears flag 0x100 vote 2. Reset when a return comes later [V]
                                                    // Written by sub_7AA160 0x7AA160. Read by sub_7AA160 0x7AA160
  /* 0x428 */ unsigned __int8 m_framesAtHome;   // sub_7AA160 counts it up to 2 while the unit is idle at home (0x7AA245) and zeroes it when a home return event starts (0x7AA22D) [V]
                                                // Written by sub_7AA160 0x7AA160. Read by sub_7AA160 0x7AA160
  /* 0x429 */ unsigned __int8 m_hitEventCount;   // FFX_Btl_OnMotionEvent 0x7B7543 counts motion event 0 (the hit frame). FFX_Btl_StepEffectReactions 0x78906F waits for it while the unit is busy. Reset by FFX_Btl_PlayUnitMotion and sub_7AB360. Debug label H [V]
                                                 // Written by Btl_OnMotionEvent 0x7B74D0, sub_7882C0 0x7882C0, sub_7AB360 0x7AB360. Read by sub_789030 0x789030, Btl_OnMotionEvent 0x7B74D0, sub_7DBEE0 0x7DBEE0
  /* 0x42A */ unsigned __int8 m_pathTurnSense;   // FFX_Btl_StartPathMove argument 5 (0x7AB082). FFX_Btl_StepUnitMove 0x7AA97B reads it signed: negative flips a turn over +90 degrees, positive flips one under -90 degrees. Type stays u8 but it is used as a signed char [V]
                                                 // Written by sub_7AB010 0x7AB010. Read by FFX_Btl_StepUnitMove 0x7AA2A0
  /* 0x42B */ unsigned __int8 m_turnTargetKind;   // what sub_7A9710 0x7A975F turns toward: 1 home facing, 2 a unit or group, 3 placement, 5 issuer, 6 the aeon out, 11 a latched point, 12 the last angle. FFX_Btl_StartUnitTurn 0x795EFA writes it, sub_795E70 sets 12 [V]
                                                  // Written by sub_795E70 0x795E70, sub_795EB0 0x795EB0, sub_7AB170 0x7AB170. Read by sub_7A9710 0x7A9710
  /* 0x42C */ unsigned __int8 m_turnKeepsDirection;   // FFX_Btl_StartUnitTurn argument 3 (0x795F0A). When set sub_7A9710 0x7A991A passes the angle through sub_7A9650, which unwraps it to keep the start turn direction, and reports tween mode 0. Exact intent [I]
                                                      // Written by sub_795EB0 0x795EB0. Read by sub_7A9710 0x7A9710
  /* 0x42D */ unsigned __int8 m_turnTargetPosLatched;   // turn kind 11: sub_7A9710 0x7A98E2 copies the issuer position into m_trackPos once and sets it, later frames reuse that point. Cleared with m_turnKeepsDirection by FFX_Btl_StartUnitTurn [V]
                                                        // Written by sub_7A9710 0x7A9710. Read by sub_7A9710 0x7A9710
  /* 0x42E */ unsigned __int8 m_turnFramesLeft;   // FFX_Btl_StartUnitTurn 0x795F26 sets 2 * ease + cruise, FFX_Btl_StepUnitMove 0x7AAA3E counts it down while turning. FFX_Btl_EndPhaseStep 0x791AF6 waits on it. Debug label DR [V]
                                                  // Written by FFX_Btl_StepUnitMove 0x7AA2A0, Btl_PlaceArrivedAeon 0x7AC6A0, sub_795EB0 0x795EB0 and 2 more. Read by FFX_Btl_StepUnitMove 0x7AA2A0, sub_795E70 0x795E70, Btl_EndPhaseStep 0x7917D0 and 1 more
  /* 0x42F */ unsigned __int8 m_turnEaseFrames;   // FFX_Btl_StartUnitTurn argument 5 (0x795F3A). FFX_Btl_StepUnitMove 0x7AAA05 passes it as the ease in and ease out lengths of the turn tween [V]
                                                  // Written by sub_795EB0 0x795EB0. Read by FFX_Btl_StepUnitMove 0x7AA2A0
  /* 0x430 */ unsigned __int8 m_turnCruiseFrames;   // FFX_Btl_StartUnitTurn argument 6, at least 1 (0x795F40). FFX_Btl_StepUnitMove 0x7AAA10 uses it as the middle part of the turn tween between the two ease parts [V]
                                                    // Written by sub_795EB0 0x795EB0. Read by FFX_Btl_StepUnitMove 0x7AA2A0
  /* 0x431 */ unsigned __int8 m_turnFaceUnit;   // unit to face for turn modes 2 and 9, 0xFF lets sub_7A9710 pick one with sub_7AA050. FFX_Btl_StartUnitTurn 0x795F2F writes it, sub_7A9710 0x7A97D1 reads it [V]
                                                // Written by sub_795EB0 0x795EB0. Read by sub_7A9710 0x7A9710
  /* 0x432 */ unsigned __int8 m_busy;   // unit is running a motion or a waited battle event. FFX_Btl_PlayUnitMotion 0x78832D and Btl 049 0x7A2D21 set it, sub_7AB360 0x7AB3D7 sets it for a waited event, Btl 018 0x7A29D4 clears it. FFX_Btl_IsUnitBusy 0x7B7E90 returns it [V]
                                        // Written by sub_7AB360 0x7AB360, AtelSys_Btl_049_resi 0x7A2D00, AtelSys_Btl_018_resi 0x7A29A0 and 3 more. Read by sub_7AF320 0x7AF320, sub_7DBEE0 0x7DBEE0, sub_78AC20 0x78AC20 and 11 more
  /* 0x433 */ unsigned __int8 m_reactionBusy;   // an interrupting event runs on the unit (hit reaction, wake 27, entry 49, event 54). FFX_Btl_PlayUnitReaction 0x78CABB sets it, FFX_Btl_ExecCommand 0x7922BB will not start the unit's command while set, sub_7AB6C0 clears it. AI property 165 [V]
                                                // AI property 165. Written by sub_78CA20 0x78CA20, Btl_OnUnitChrLoaded 0x7839B0, BtlAi_SetProperty 0x7B4B70 and 2 more. Read by BtlAi_GetProperty 0x7B2DC0, Btl_ExecCommand 0x792210, sub_784220 0x784220 and 2 more
  /* 0x434 */ unsigned __int8 m_motionNotStarted;   // 1 when the last motion request did not start or has no frames. sub_7A8FC0 0x7A8FFE sets 1 and 0x7A9123 clears it once a motion starts, FFX_Btl_PlayUnitMotion 0x7883D5 writes frames <= 0. sub_7A8C50 0x7A8C65 stops waiting on it [V]
                                                    // Written by sub_7A8FC0 0x7A8FC0, sub_7882C0 0x7882C0. Read by sub_7878A0 0x7878A0, Btl_FinishActions 0x7911E0, AtelSys_Btl_163_resi 0x7A3730 and 2 more
  /* 0x435 */ unsigned __int8 m_mixPrepareBusy;   // the Mix prepare event is running. FFX_Btl_ExecCommand 0x792418 sets it in record state 4 (set by FFX_Btl_StartMixPrepareLoad) with event 29, Btl 018 0x7A29C8 clears it, sub_7B0520 0x7B0544 waits on it [V]
                                                  // Written by AtelSys_Btl_018_resi 0x7A29A0, Btl_ExecCommand 0x792210. Read by sub_788480 0x788480, sub_7B0520 0x7B0520
  /* 0x436 */ unsigned __int8 m_dying;   // death sequence running. sub_78DC30 0x78DC72 sets it when it starts the KO event (57, 58, 64 or 8), sub_78CCA0 0x78CCAF clears it when done. FFX_Btl_ExecCommand 0x7923F4, FFX_Btl_PlayUnitReaction 0x78CA57 and FFX_Btl_EndPhaseStep 0x791CAA wait on it [V]
                                         // Written by sub_78DC30 0x78DC30, sub_7B7E20 0x7B7E20, Btl_SwapUnitIntoPartySlot 0x7ADAE0 and 1 more. Read by sub_788480 0x788480, sub_787950 0x787950, Btl_ExecCommand 0x792210 and 9 more
  /* 0x437 */ unsigned __int8 m_neckAimEnabled;   // the unit turns its neck toward a single target. FFX_Btl_AimNeckAtSingleTarget 0x7B1EA4 does nothing while it is 0, FFX_Btl_SetupUnitRoster 0x79C538 sets 1. AI property 335 [V]
                                                  // AI property 335. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by sub_7B1E80 0x7B1E80, BtlAi_GetProperty 0x7B2DC0
  /* 0x438 */ unsigned __int8 m_neckTarget;   // unit the neck looks at, 0xFF none. FFX_Btl_AimNeckAtSingleTarget 0x7B1EDB, Btl 258 0x7A362C write it, sub_7B1F10 0x7B1F26 resets it. sub_7AAB80 0x7AABE5 aims at it when m_neckAimDelay runs out [V]
                                              // Written by sub_7B1E80 0x7B1E80, FFX_AtelSys_Btl_258_SetNeckTarget_resi 0x7A35D0, sub_78DC30 0x78DC30 and 2 more. Read by sub_7AAB80 0x7AAB80, sub_7B1F10 0x7B1F10
  /* 0x439 */ unsigned __int8 m_neckAimDelay;   // frames before the neck look-at is applied. FFX_Btl_AimNeckAtSingleTarget 0x7B1EE2 sets 5, Btl 258 and sub_7B1F10 set 1. sub_7AAB80 0x7AABDD counts it down and at 0 aims at m_neckTarget or resets the neck [V]
                                                // Written by sub_7B1F10 0x7B1F10, sub_7B1E80 0x7B1E80, FFX_AtelSys_Btl_258_SetNeckTarget_resi 0x7A35D0 and 1 more. Read by sub_7AAB80 0x7AAB80
  /* 0x43A */ unsigned __int8 m_victoryPosePlayed;   // set once AI event 45 (victory pose) has been started for the unit. FFX_Btl_EndPhaseStep 0x791D38 sets it and 0x791D07 skips units that have it [V]
                                                     // Written by Btl_EndPhaseStep 0x7917D0. Read by Btl_EndPhaseStep 0x7917D0
  /* 0x43B */ unsigned __int8 m_victoryPoseEnabled;   // unit plays AI event 45 at victory. FFX_Btl_EndPhaseStep 0x791CFE requires it, FFX_Btl_SetupUnitRoster 0x79C22C sets 1. AI property 220 [V]
                                                      // AI property 220. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_EndPhaseStep 0x7917D0
  /* 0x43C */ unsigned __int8 m_b43C;   // AI property 245. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0
  /* 0x43D */ unsigned __int8 m_spawnFlags;   // appearance flags. Bit 1 hides the CHR, 4 hides it, 8 hides a new CHR (maybe_FFX_Btl_SetupUnitChr 0x793869). Bit 2 plays entry event 49 instead of a fade-in (FFX_Btl_OnUnitChrLoaded 0x783A84). Roster 3 enemy, 8 others. AI property 134 [V]
                                              // AI property 134. Written by Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_265_resi 0x7A3DF0, Btl_SwapUnitIntoPartySlot 0x7ADAE0 and 1 more. Read by Btl_OnUnitChrLoaded 0x7839B0, BtlAi_GetProperty 0x7B2DC0, maybe_Btl_SetupUnitChr 0x793670
  /* 0x43E */ unsigned __int8 m_holdPositionReentry;   // keeps the unit where it is. sub_7AAAE0 0x7AAB54 does not test the home distance while set, FFX_Btl_PlayUnitReaction 0x78CADE skips the position snap. FFX_Btl_ResetUnitForReentry 0x78D5FF sets it, sub_7AB6C0 0x7AB6D6 clears it. AI property 94 [V]
                                                       // AI property 94. Written by sub_7A89C0 0x7A89C0, BtlAi_SetProperty 0x7B4B70, Btl_ResetUnitForReentry 0x78D530 and 1 more. Read by BtlAi_GetProperty 0x7B2DC0, sub_78CA20 0x78CA20, sub_7DBEE0 0x7DBEE0 and 1 more
  /* 0x43F */ unsigned __int8 m_attachParentUnit;   // unit this one rides on, 0xFF none. sub_796EA0 0x796EA0 takes the position from the parent's bone
                                                    // m_attachBone, sub_7AAB80 0x7AAB80 calls FFX_Ch_AttachToParentBone. Btl 082 writes all three
  /* 0x440 */ unsigned __int8 m_hasAttachedUnits;   // set on a parent. The visibility, alpha and colour passes (FFX_Btl_SetUnitShowVote, sub_7962D0, sub_796480) then
                                                    // recurse into every unit whose m_attachParentUnit is this one
  /* 0x441 */ unsigned __int8 m_attachBone;   // parent bone index for m_attachParentUnit (FFX_Ch_GetBoneWorldPos, FFX_Ch_AttachToParentBone)
  /* 0x442 */ unsigned __int8 m_popupBone;   // bone the damage popup is anchored to, 0xFF uses sub_794AB0. sub_79F380 0x79F4B8 projects it to +0xF44 for sub_79F660, which draws the dword_11360BC number list. Roster 0x79C256 sets 0xFF. AI property 338 [V]
                                             // AI property 338. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_79F380 0x79F380
  unsigned __int8 _unk_443[0x3];
  /* 0x446 */ unsigned __int8 m_actionKind;   // how the current action plays, from the ability's +21 kind. 0 event script, 1 attack motion, 2 magic effect, 3 magic effect after the in-place test. FFX_Btl_StartCommandExec 0x7AEC15 writes it, sub_7B75C0 applies motion hits for 1 and 4. AI property 224 [V]
                                              // AI property 224. Written by Btl_CmdQueue_RemoveAt 0x7B0860, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_78D8D0 0x78D8D0, sub_7B75C0 0x7B75C0 and 4 more
  /* 0x447 */ unsigned __int8 m_hitReactFlags;   // bit 1 picks the physical hit reaction 4 (event 15), bit 2 the magic hit reaction 3 (event 14), else reaction 5 (event 9). maybe_FFX_Btl_PickReaction 0x78C2D9 reads it, FFX_Btl_SetupUnitRoster 0x79C5CB sets 3 for enemies. AI properties 119..120 [V]
                                                 // AI properties 119..120. Written by BtlAi_SetProperty 0x7B4B70, Btl_SetupUnitRoster 0x79C110. Read by BtlAi_SetProperty 0x7B4B70, BtlAi_GetProperty 0x7B2DC0, maybe_FFX_Btl_PickReaction 0x78C270
  /* 0x448 */ unsigned __int8 m_b448;   // AI property 122. Written by sub_7AB360 0x7AB360, sub_7882C0 0x7882C0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7AB360 0x7AB360, sub_7DBEE0 0x7DBEE0
  /* 0x449 */ unsigned __int8 m_motionReleaseMode;   // maybe_FFX_Btl_ReleaseUnitMotion 0x7879C8 latches its mode argument when it is 1 or 6. sub_78DC30 0x78DCA3 reads it when death starts, 1 and 6 skip the normal KO event. FFX_Btl_ResetUnitForReentry 0x78D5C5 clears it [V]
                                                     // Written by sub_787950 0x787950, Btl_ResetUnitForReentry 0x78D530. Read by sub_78DC30 0x78DC30
  /* 0x44A */ unsigned __int8 m_attackInPlace;   // the attack is done without running to the target. sub_7AF230 0x7AF253 sets it from m_forceAttackInPlace or the attack_stay distance test sub_7AF0F0, FFX_Btl_StartCommandExec 0x7AE456 clears it. sub_7B75C0 0x7B760C picks hit effect 31 not 3. AI property 247 [V]
                                                 // AI property 247. Written by Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70, sub_7AF230 0x7AF230. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B75C0 0x7B75C0
  /* 0x44B */ unsigned __int8 m_forceAttackInPlace;   // when set sub_7AF230 0x7AF237 copies it into m_attackInPlace and skips the distance test. AI property 250 [V]
                                                      // AI property 250. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7AF230 0x7AF230
  /* 0x44C */ unsigned __int8 m_radiusScaleSet;   // m_radiusScale was set by script. FFX_BtlAi_SetUnitFloat 0x7B4B19 index 9 sets it, maybe_FFX_Btl_SetupUnitChr 0x79372E then keeps the value instead of reloading it from the CHR data [V]
                                                  // Written by BtlAi_SetUnitFloat 0x7B4A90. Read by maybe_Btl_SetupUnitChr 0x793670
  /* 0x44D */ unsigned __int8 m_popupHeightSet;   // m_popupHeight was set by script. FFX_BtlAi_SetProperty 0x7B6123 (property 261) sets it, maybe_FFX_Btl_SetupUnitChr 0x793746 then keeps the value instead of height * 13 / 14 [V]
                                                  // Written by BtlAi_SetProperty 0x7B4B70. Read by maybe_Btl_SetupUnitChr 0x793670
  /* 0x44E */ unsigned __int8 m_hitReactSide;   // side of the last directional hit reaction, +1 or -1, 0 none. FFX_Btl_PlayUnitReaction writes 1 or -1 for reactions 13..16 (0x78CB49..0x78CB83) and 0 first. Btl 013 0x7A643B reads it. AI property 161. Signed in use [V]
                                                // AI property 161. Written by sub_78CA20 0x78CA20, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, AtelSys_Btl_013_resi 0x7A63F0
  /* 0x44F */ unsigned __int8 m_b44F;   // AI property 289. Written by sub_787950 0x787950, Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_038_resi 0x7A5D80 and 3 more. Read by BtlAi_GetProperty 0x7B2DC0, AtelSys_Btl_039_resi 0x7A5E90, sub_7DBEE0 0x7DBEE0
  /* 0x450 */ unsigned __int8 m_weakPoseSuppressed;   // forces m_weakPose to 0 in sub_78D7F0 0x78D814. FFX_Btl_StepMagicEffectStates 0x78859A sets it when the action targets others, maybe_FFX_Btl_ReleaseUnitMotion 0x7879EF and FFX_Btl_FinishActions 0x7914FE clear it [V]
                                                      // Written by sub_788480 0x788480, sub_787950 0x787950, Btl_FinishActions 0x7911E0. Read by sub_78D7F0 0x78D7F0
  /* 0x451 */ unsigned __int8 m_b451;   // AI property 133. Written by sub_7B1AA0 0x7B1AA0, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B1AA0 0x7B1AA0
  /* 0x452 */ unsigned __int16 m_moveFrame;   // frame counter of the scripted move, 0 at start (FFX_Btl_StartUnitMove), counted by FFX_Btl_StepUnitMove
  /* 0x454 */ unsigned __int16 m_moveAccelFrames;   // accelerating part, total * arg7 / 128 (FFX_Btl_StartUnitMove). For move kind 7 it is the spline index
  /* 0x456 */ unsigned __int16 m_moveCruiseFrames;   // the frames left after the accelerating and decelerating parts (FFX_Btl_StartUnitMove)
  /* 0x458 */ unsigned __int16 m_moveDecelFrames;   // decelerating part, total * arg8 / 128 (FFX_Btl_StartUnitMove)
  /* 0x45A */ unsigned __int16 m_moveTotalFrames;   // move length in frames (FFX_Btl_StartUnitMove), FFX_Btl_StepUnitMove ends the move when m_moveFrame reaches it
  /* 0x45C */ float m_moveStartSpeed;   // FFX_Btl_StartUnitMove argument 5, or m_moveSpeed when bit 0x40 of the kind is set. sub_7AAC40 0x7AAC40 reads it
  /* 0x460 */ float m_moveSpeed;   // current speed of a homing or orbit move, raised by m_moveSpeedAccel up to m_moveSpeedMax (FFX_Btl_StepUnitMove)
  /* 0x464 */ float m_moveSpeedMax;   // see m_moveSpeed
  /* 0x468 */ float m_moveSpeedAccel;   // see m_moveSpeed
  /* 0x46C */ float m_moveYaw;   // orbit yaw of a polar move, slewed toward the target by m_moveYawStep (FFX_Btl_StepUnitMove)
  /* 0x470 */ float m_moveYawStep;   // the yaw slew limit, itself raised by m_moveYawStepAccel every frame
  /* 0x474 */ float m_moveYawStepAccel;   // see m_moveYawStep
  /* 0x478 */ float m_movePitch;   // orbit pitch, slewed like m_moveYaw while m_groundMode is 0 (underwater)
  /* 0x47C */ float m_movePitchStep;   // see m_movePitch
  /* 0x480 */ float m_movePitchStepAccel;   // see m_movePitch
  /* 0x484 */ float m_moveTurnRate;   // when non-zero FFX_Btl_StepUnitMove turns m_facing toward m_moveYaw by at most this per frame
  /* 0x488 */ float m_moveArg9;   // FFX_Btl_StartUnitMove argument 9, handed back when the move restarts
  /* 0x48C */ float m_moveArg10;   // FFX_Btl_StartUnitMove argument 10, handed back when the move restarts
  /* 0x490 */ float m_facing;   // facing angle in radians. FFX_Btl_StepUnitMove pushes it with FFX_Ch_SetRotAndMoveDir, Btl 263 measures from it,
                                // sub_7AB010 0x7AB010 fills it from FFX_Btl_GetUnitRot
  /* 0x494 */ float m_facingDelta;   // Btl 044 and sub_795EB0 0x795EB0 write a turn amount, sub_7A9650 0x7A9650 compares it with 2 pi / 3
  /* 0x498 */ float m_moveArg11;   // FFX_Btl_StartUnitMove argument 11, handed back when the move restarts
  /* 0x49C */ float m_returnFacing;   // the facing to return to: sub_793260 0x793260 writes m_homeFacing[i] + pi, FFX_Btl_StartCommandExec
                                      // writes sub_7A99B0, read by sub_7958C0 0x7958C0
  /* 0x4A0 */ float m_moveProgress;   // 0 at start (FFX_Btl_StartUnitMove), sub_7AAC40 measures m_moveDistance - m_moveProgress
  /* 0x4A4 */ float m_moveDistance;   // horizontal or 3D length of the move, FFX_Btl_StartUnitMove
  /* 0x4A8 */ float m_moveGravity;   // FFX_Btl_StartUnitMove argument 12, the arc acceleration FFX_Btl_StepUnitMove applies when m_moveUseGravity is set
  /* 0x4AC */ int m_motionType;   // script stat stat_motion_type, the motion_type_* flags SG_DebugWin_WriteMotionMacro prints: 0x1 attack_run_00_01,
                                  // 0x2 attack_run_01, 0x4 attack_miss, 0x8 attack_return, 0x10 attack_pos_check, 0x20 avoid_move,
                                  // 0x40 hokan_wait_to_run, 0x80 hokan_normal, 0x100 move_back_jump, 0x200 move_avoid, 0x400 turn_stay,
                                  // 0x800 turn_run, 0x1000 attack_stay, 0x2000 wait_return, 0x4000 disable_avoid,
                                  // 0x8000 disable_chest_move, 0x10000 attack_weight_max. 0x60006 at roster setup. AI properties 158, 292..293
  /* 0x4B0 */ unsigned __int16 m_round;   // script stat stat_round, debug label "ROUND : Frame". AI property 218
  /* 0x4B2 */ unsigned __int16 m_roundReturn;   // script stat stat_round_return, debug label "ROUND : Return Frame". AI property 219
  /* 0x4B4 */ unsigned __int8 m_inPlaceSkipSelf;   // when set the attack_stay test sub_7AF0F0 0x7AF150 does not count the attacker itself as a close target. AI property 253 [V]
                                                   // AI property 253. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7AF0F0 0x7AF0F0
  /* 0x4B5 */ unsigned __int8 m_weaponAttachable;   // on the owner unit, 1 when its weapon CHR rides a hand bone, 0 when the stand-in is unit 30. FFX_Btl_SetupWeaponChr 0x793B1F writes it, sub_793330 0x79337A attaches and sub_793F10 0x793F76 detaches only while set [V]
                                                    // 1 on units 28..30 when FFX_Btl_SetupWeaponChr hands them a weapon CHR. sub_793330 and sub_793F10 need it
  /* 0x4B6 */ unsigned __int8 m_weaponDetached[2];   // per side record, the held CHR was dropped into the world. sub_793F10 0x793F86 sets it, sub_793330 0x7933AB clears it on re-attach. sub_791170 0x791176 makes FFX_Btl_ExecCommand wait on unit 5 while set [V]
                                                     // two flags indexed by side record, set by sub_793F10 0x793F86, read by sub_793330 and sub_791170
  /* 0x4B8 */ char m_idle2Prob;   // script stat stat_idle2_prob, debug label "IDLE2 : Prob". 4 at setup. AI property 228
  /* 0x4B9 */ unsigned __int8 m_attackIncSpeed;   // script stat stat_attack_inc_speed, debug label "ATTACK : Inc Speed". 0x20 at setup. AI property 230
  /* 0x4BA */ unsigned __int8 m_attackDecSpeed;   // script stat stat_attack_dec_speed, debug label "ATTACK : Dec Speed". 0x20 at setup. AI property 231
  /* 0x4BB */ unsigned __int8 m_motionNum;   // script stat stat_motion_num, debug label "HOKAN : Type". sub_79CD80 0x79CD80 turns it into m_motionTable. AI property 234
  /* 0x4BC */ float m_attackStartDist;   // setmotiondata2 motion_attack_start_dist, index 0 of Btl 178 (BtlAi_SetUnitFloat). 40.0 at setup
  /* 0x4C0 */ float m_attackOffset;   // motion_attack_offset, index 1
  /* 0x4C4 */ float m_attackHeight;   // motion_attack_height, index 2
  /* 0x4C8 */ float m_backJumpDist;   // motion_move_backjump_dist, index 3
  /* 0x4CC */ float m_runSpeedV0;   // motion_run_speed_v0, debug label "RUN : First Speed", index 4
  /* 0x4D0 */ float m_runSpeedAcc;   // motion_run_speed_acc, index 5. 0.4 at setup
  /* 0x4D4 */ float m_runSpeed;   // motion_run_speed, index 6. 3.0 at setup
  /* 0x4D8 */ float m_runSpeedReturn;   // motion_run_speed_return, index 7. 3.0 at setup
  /* 0x4DC */ float m_weight;   // motion_weight, index 8. 50.0 at setup. Pushed to CHR.m_pushWeight with FFX_Ch_SetF500
  unsigned __int8 _unk_4E0[0x4];
  /* 0x4E4 */ float m_radiusScale;   // collision radius scale. maybe_FFX_Btl_SetupUnitChr 0x79373D loads FFX_Ch_GetDataF20 and pushes it with FFX_Ch_SetCollisionRadiusScale, FFX_Btl_GetUnitRadius 0x795A3B returns it times m_scale[2]. Btl 178 index 9 sets it [V]
                                     // the last Btl 178 float. sub_795A20 0x795A20 returns it times m_scale[2]
  /* 0x4E8 */ float m_popupHeight;   // height above m_popupBone for the damage popup anchor. sub_79F380 0x79F4ED subtracts it from y. maybe_FFX_Btl_SetupUnitChr 0x793764 sets height * 13 / 14. AI property 261 in hundredths [V]
                                     // AI property 261. Written by maybe_Btl_SetupUnitChr 0x793670, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_79F380 0x79F380
  /* 0x4EC */ float m_uiAnchorHeight;   // height above the ground of the point FFX_BtlUi_ProjectUnitToScreen 0x795314 projects to +0xF54 for the battle menu UI. AI property 296 in hundredths [V]
                                        // AI property 296. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_795250 0x795250, sub_7DBEE0 0x7DBEE0
  /* 0x4F0 */ float m_homeTolerance;   // distance from the placement point past which the unit counts as away from home. sub_7A9230 0x7A9281 compares it, FFX_Btl_SetupUnitRoster 0x79C323 sets 0.2, Btl 142 0x7A5C64 uses 10.0 for a sleeping airborne unit [V]
                                       // Written by AtelSys_Btl_142_resi 0x7A5C20, Btl_SetupUnitRoster 0x79C110. Read by AtelSys_Btl_142_resi 0x7A5C20, sub_7A9230 0x7A9230
  /* 0x4F4 */ float m_turnTargetFacing;   // the facing FFX_Btl_StepUnitMove 0x7AAA74 last computed for a scripted turn, reused as the target by turn mode 12 in sub_7A9710 0x7A990E [V]
                                          // Written by FFX_Btl_StepUnitMove 0x7AA2A0. Read by sub_7A9710 0x7A9710
  /* 0x4F8 */ void *m_motionTable;   // pointer into the motion table at dword_112A954 for m_motionNum (sub_79CD80), read by sub_79CB40 0x79CB40
  /* 0x4FC */ char m_placeLayout;   // the battle position key (layout, group, slot) sub_7ABFE0 0x7ABFE0 turns into a position
                                    // (maybe_FFX_Btl_GetUnitTargetPos). AI property 28
  /* 0x4FD */ char m_placeGroup;   // 3 characters, 4 aeons, 5 enemies at roster setup. FFX_Btl_PartyFlee adds 3
  /* 0x4FE */ char m_placeSlot;   // battle party slot 0..2, aeon index, or the running enemy index (FFX_Btl_SetupUnitRoster). AI property 29
  /* 0x4FF */ char m_protectLevel;   // target protection level, compared in FFX_Btl_ResolveTargets 0x79202D (BATTLE_COMMAND.md). AI property 30
  /* 0x500 */ char m_savedPlace[3];   // m_placeLayout, m_placeGroup and m_placeSlot saved by FFX_Btl_SwapUnitIntoPartySlot and restored by
                                      // FFX_Btl_ReturnPartyAfterSummon
  unsigned __int8 _unk_503[0x1];
  /* 0x504 */ unsigned __int8 m_targetGroup;   // target group 0..7. sub_795040 0x795060 returns it, sub_799D50 keeps only targets in the provoker's group for a group target ability. Debug label GP. AI property 31 [V]
                                               // AI property 31. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_795040 0x795040, BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0
  /* 0x505 */ unsigned __int8 m_airborne;   // unit is airborne. FFX_Btl_IsUnitAirborne 0x794F90 returns it, the centre and top position getters (0x794C54, 0x7959B4) treat the position as the centre. Btl 041 0x7A3FE2 and Btl 265 0x7A3E7D set it. AI property 26 [V]
                                            // AI property 26. Written by AtelSys_Btl_265_resi 0x7A3DF0, BtlAi_SetProperty 0x7B4B70, AtelSys_Btl_041_resi 0x7A3FB0. Read by sub_79E8C0 0x79E8C0, maybe_Btl_GetUnitCenterPos 0x794C20, AtelSys_Btl_142_resi 0x7A5C20 and 6 more
  /* 0x506 */ unsigned __int8 m_resetPosOnSetup;   // when set maybe_FFX_Btl_SetupUnitChr 0x7936E1 moves the CHR to the placement point even if the CHR already existed. AI property 343 [V]
                                                   // AI property 343. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, maybe_Btl_SetupUnitChr 0x793670
  unsigned __int8 _unk_507[0x1];
  /* 0x508 */ unsigned __int8 m_awayFromHome;   // unit is farther than m_homeTolerance from its placement point. sub_7AA100 0x7AA125 recomputes it, sub_7AA160 0x7AA1CF then sends it back with event 0, FFX_Btl_ExecCommand 0x7923E7 waits on it. Btl 042 returns it. AI property 95 [V]
                                                // AI property 95. Written by Btl_OnUnitChrReloaded 0x783BC0, sub_7AA100 0x7AA100, AtelSys_Btl_047_resi 0x7A5140 and 4 more. Read by AtelSys_Btl_142_resi 0x7A5C20, sub_7AF8A0 0x7AF8A0, sub_78AC20 0x78AC20 and 17 more
  /* 0x509 */ char m_altPlace[3];   // a second (layout, group, slot) key. sub_7AC600 0x7AC600 copies it into the live one. AI properties 85..86
  /* 0x50C */ unsigned __int8 m_yOffsetMode;   // how sub_7AB300 0x7AB314 sets m_yOffset. 0 ground - height / 2 + m_yOffsetBase, 1 zero, other keeps it. Btl 010 0x7A4144 writes it, roster 0x79C2DE sets 1, Btl 265 sets 0 [V]
                                               // Written by AtelSys_Btl_010_resi 0x7A4100, Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_265_resi 0x7A3DF0. Read by sub_7AB300 0x7AB300
  /* 0x50D */ unsigned __int8 m_keepYOffset;   // applies m_yOffset even while the unit is asleep or down (FFX_Btl_GetUnitPlacementPos 0x79494A, FFX_Btl_GetUnitYOffset 0x7949D4). Cleared when a motion or event starts (0x788326, 0x7AB3A7). AI property 96 [V]
                                               // AI property 96. Written by sub_7AB360 0x7AB360, sub_78D680 0x78D680, sub_7882C0 0x7882C0 and 1 more. Read by maybe_Btl_GetUnitTargetPos 0x7948E0, maybe_Btl_GetUnitYOffset 0x794990, BtlAi_GetProperty 0x7B2DC0 and 2 more
  /* 0x50E */ unsigned __int8 m_wakingUp;   // the wake from sleep event is pending or running. FFX_Btl_TurnEndStatusTick 0x7AF446 sets it with event 27 when the sleep count ends, FFX_Btl_PlayUnitReaction 0x78CA5E copies m_wokenByHit. FFX_Btl_ExecCommand 0x7922C8 waits on it. AI property 97 (word write) [V]
                                            // AI property 97. Written by sub_78CA20 0x78CA20, BtlAi_SetProperty 0x7B4B70, Btl_TurnEndStatusTick 0x7AF390 and 1 more. Read by sub_7DBEE0 0x7DBEE0, BtlAi_GetProperty 0x7B2DC0, Btl_ExecCommand 0x792210
  /* 0x50F */ unsigned __int8 m_wokenByHit;   // a physical hit cleared the target's sleep. FFX_Btl_WakeOnPhysicalHit 0x78C135 sets it, FFX_Btl_PlayUnitReaction 0x78CA4E moves it into m_wakingUp and clears it [V]
                                              // Written by sub_78CA20 0x78CA20, FFX_Btl_WakeOnPhysicalHit 0x78C0F0. Read by sub_78CA20 0x78CA20
  /* 0x510 */ unsigned __int8 m_facingOverrideOn;   // sub_7958C0 0x7958E9 returns m_facingOverride instead of m_returnFacing while set. FFX_Btl_SetUnitHomeFacingOverride 0x796022 writes it, sub_793E00 0x793E3D clears it [V]
                                                    // Written by sub_793E00 0x793E00, sub_795FF0 0x795FF0. Read by sub_7958C0 0x7958C0, sub_7DBEE0 0x7DBEE0
  /* 0x511 */ char m_facingIndex;   // index into m_homeFacing and m_homeFacing2
  /* 0x512 */ unsigned __int8 m_homeFacingFixed;   // script fixed the home facing. Btl 050 0x7A6FE4 sets it with m_homeFacing, sub_793CF0 0x793D35 then skips the automatic re-facing of enemy units 20..27 [V]
                                                   // Written by AtelSys_Btl_050_resi 0x7A6F70. Read by sub_793CF0 0x793CF0
  unsigned __int8 _unk_513[0x1];
  /* 0x514 */ float m_homeFacing[4];   // facing per position, picked by m_facingIndex (sub_7948A0 0x7948A0, magic host API 169, returns the
                                       // current one). sub_793260 adds pi to it for m_returnFacing
  /* 0x524 */ float m_homeFacing2[4];   // a second value per position, read with m_homeFacing by sub_794060 0x794060 case 6
  /* 0x534 */ float m_yOffset;   // height offset added to the target point (maybe_FFX_Btl_GetUnitTargetPos, maybe_FFX_Btl_GetUnitYOffset).
                                 // sub_7AB300 0x7AB300 sets it to ground - height / 2 + m_yOffsetBase
  /* 0x538 */ float m_yOffsetBase;   // written by Btl 010 and Btl 265, read by sub_7AB300
  /* 0x53C */ float m_facingOverride;   // facing returned by sub_7958C0 0x7958F2 while m_facingOverrideOn is set. FFX_Btl_SetUnitHomeFacingOverride 0x796016 writes it [V]
                                        // Written by sub_795FF0 0x795FF0. Read by sub_7958C0 0x7958C0
  /* 0x540 */ char m_battleName[40];   // the name shown in battle, 40 bytes of BTLCHR.m_name for a monster (FFX_Btl_LoadUnitParams) or the
                                       // character name. Btl 220 writes it
  /* 0x568 */ unsigned __int8 m_battleNameSpare[40];   // 2026-10-28: spare tail of the name buffer, zero. Only the bulk zeroings write it, FFX_Btl_ZeroBytes(unit + 0x540, 0x1EC)
                                                       // at 0x79B513 and FFX_Btl_AllocActorArrays. No instruction in FFX.exe addresses it, directly or through a sub-pointer, and no
                                                       // magic DLL does [M] (BATTLE_ACTOR.md 9.1). The four name writers stop at 40 bytes (0x79B78D, 0x79B55B, 0x7A7A7B, 0x7A7820),
                                                       // so byte 0x568 is the zero terminator of a 40 byte monster name, the text draws read up to it [V].
                                                       // First pass note: no reader or writer found. FFX_Btl_LoadUnitParams 0x79B4F0 zeroes it with the rest of 0x540..0x72B.
                                                       // Probably the tail of an 80 byte name buffer, the copies only fill 40 bytes (monster, Btl 220) or 20
                                                       // (character, from g_ffxCharNames)
  /* 0x590 */ unsigned __int8 m_romFlags;   // ply_rom record +16 flag byte for a character (Btl_LoadCharRecordIntoActor). Bit 2 is read by sub_79ADB0. AI properties 22..24
  /* 0x591 */ unsigned __int8 m_partyFlag;   // character record +44 (Btl_LoadCharRecordIntoActor). Booster_InvincibleRefillUnit tests it for 17
  /* 0x592 */ unsigned __int8 m_equipIds[2];   // equipped weapon [0] and armour [1] entry index. sub_7ABDC0 0x7ABDC0 turns each into the model id of
                                               // side record 0 and 1 (FFX_Btl_SetupUnitRoster, sub_7AD820 0x7AD820)
  /* 0x594 */ int m_maxHp;   // max HP after equipment. AI property 2
  /* 0x598 */ int m_maxMp;   // max MP after equipment. AI property 3
  /* 0x59C */ int m_baseMaxHp;   // max HP before the break limit cap, character record +36 (Btl_LoadCharRecordIntoActor). Capped at 9999
                                 // or 99999 by auto-ability word 2 bit 0x200
  /* 0x5A0 */ int m_baseMaxMp;   // max MP before the cap, record +40. Capped at 999 or 9999 by auto-ability word 2 bit 0x400
  /* 0x5A4 */ int m_overkillThreshold;   // the HP the OVERKILL test compares with, copied to m_overkillHp per action
                                         // (FFX_Btl_ApplyActionToTargets 0x789437). AI property 268
  /* 0x5A8 */ unsigned __int8 m_strength;   // strength. The eight stats 0x5A8..0x5AF are AI properties 9..16 and are loaded with their bonus bytes
                                            // m_statBonus added and clamped to 1..255 (Btl_LoadCharRecordIntoActor)
  /* 0x5A9 */ unsigned __int8 m_defense;   // defense. AI property 10
  /* 0x5AA */ unsigned __int8 m_magic;   // magic. AI property 11
  /* 0x5AB */ unsigned __int8 m_magicDefense;   // magic defense. AI property 12
  /* 0x5AC */ unsigned __int8 m_agility;   // agility, the CTB tick lookup and the turn order sort key (FFX_Btl_CtbSortKey 0x78F01A). AI property 13
  /* 0x5AD */ unsigned __int8 m_luck;   // luck. AI property 14
  /* 0x5AE */ unsigned __int8 m_evasion;   // evasion. AI property 15
  /* 0x5AF */ unsigned __int8 m_accuracy;   // accuracy. AI property 16
  /* 0x5B0 */ unsigned __int8 m_statBonus[8];   // per stat bonus added at load, in the m_strength..m_accuracy order (AI properties 302..309)
  /* 0x5B8 */ unsigned __int16 m_monsterFlags;   // monster flag word, monster param +40 (FFX_Btl_LoadUnitParams). Bit 8 is tested by sub_78F990 0x78F990. AI properties 32..36, 240, 263..265, 267, 290
  /* 0x5BA */ unsigned __int8 m_poisonPercent;   // poison damage percent of max HP (Btl_ApplyPoisonDamage 0x7AFADE). AI property 17
  /* 0x5BB */ unsigned __int8 m_overdriveMode;   // overdrive mode index (FFX_Btl_ApplyVictorOverdrive tests 11). AI property 18
  /* 0x5BC */ unsigned __int8 m_overdrive;   // overdrive gauge. AI properties 19, 280
  /* 0x5BD */ unsigned __int8 m_overdriveMax;   // overdrive gauge max. AI property 20
  /* 0x5BE */ unsigned __int8 m_savedOverdrive;   // gauge saved for the temporary force full (Btl_GrantAeonFullGauge, Btl_RestoreAeonGauge)
  /* 0x5BF */ unsigned __int8 m_overdriveForced;   // 1 while that temporary override is active
  /* 0x5C0 */ unsigned __int8 m_usingOverdrive;   // the current command spends overdrive gauge. FFX_Btl_CheckCommandCost 0x78ABC9 sets it from the ability's gauge cost byte +38, FFX_Btl_StartMixPrepareLoad 0x7AFC24 sets 1. sub_79E8C0 0x79E911 picks aura effect 52 or 53 [V]
                                                  // Written by sub_7B1AA0 0x7B1AA0, Btl_CheckCommandCost 0x78AB20, sub_7AFBD0 0x7AFBD0. Read by sub_79E8C0 0x79E8C0, sub_7B1AA0 0x7B1AA0
  /* 0x5C1 */ unsigned __int8 m_weaponDmgFormula;   // weapon damage formula, used instead of the ability's +40 when the ability uses weapon properties (0x40000). Equip entry +8 (FFX_Btl_LoadCharRecordIntoActor 0x79C867), default 1. Read by FFX_Btl_ComputeEffectForTarget 0x78E75F [V]
                                                    // Written by Btl_LoadCharRecordIntoActor 0x79C5F0, Btl_LoadUnitParams 0x79B4F0. Read by Btl_CtbPreview_ApplyPendingCmd 0x7898D0, sub_78CD30 0x78CD30, Btl_ComputeEffectForTarget 0x78E630
  /* 0x5C2 */ unsigned __int8 m_b5C2;   // Written by Btl_LoadUnitParams 0x79B4F0, sub_7A89C0 0x7A89C0. Read by Btl_OnUnitChrLoaded 0x7839B0, sub_7A89C0 0x7A89C0, sub_79AE30 0x79AE30 and 1 more
  /* 0x5C3 */ unsigned __int8 m_b5C3;   // AI property 259. Written by Btl_OnUnitChrLoaded 0x7839B0, Btl_LoadUnitParams 0x79B4F0, sub_7A89C0 0x7A89C0 and 1 more. Read by sub_7A89C0 0x7A89C0, BtlAi_GetProperty 0x7B2DC0, sub_79AE10 0x79AE10 and 1 more
  /* 0x5C4 */ unsigned __int8 m_provoker;   // the provoking unit, the forced target of status 1 bit 0x400. AI property 257
  /* 0x5C5 */ unsigned __int8 m_threatener;   // the threatening unit (on the target), sub_78E460 0x78E460
  /* 0x5C6 */ unsigned __int8 m_threatTarget;   // the threatened unit (on the threatener), sub_78E410 0x78E410
  /* 0x5C7 */ unsigned __int8 m_weaponPower;   // weapon attack power, used instead of the ability's +42 power for weapon-property abilities. Equip entry +9 (FFX_Btl_LoadCharRecordIntoActor 0x79C871), default 16. Read by FFX_Btl_ComputeEffectForTarget 0x78E770 [V]
                                               // Written by Btl_LoadCharRecordIntoActor 0x79C5F0. Read by Btl_CtbPreview_ApplyPendingCmd 0x7898D0, Btl_ComputeEffectForTarget 0x78E630
  /* 0x5C8 */ unsigned __int8 m_doomCount;   // doom countdown, live (sub_799CD0). AI property 160
  /* 0x5C9 */ unsigned __int8 m_doomStart;   // doom start count, copied to m_doomCount when Doom lands. AI property 159
  /* 0x5CA */ unsigned __int8 m_autoLifeArmed;   // set with status 2 bit 0x200, Auto-Life (sub_78B4E0, FFX_Btl_ScaleMagicBooster, sub_7B2910 0x7B2910). FFX_Btl_ScaleMagicBooster clears it for command 0x311F and scales that hit 1.5x
  /* 0x5CB */ unsigned __int8 m_ignoreProvoke;   // when set Provoke (status 1 bit 0x400) does not force the target. sub_799D50 0x799DAA skips the forced command and Btl 011 0x7A44EE still lets the AI add commands. AI property 256 [V]
                                                 // AI property 256. Written by BtlAi_SetProperty 0x7B4B70. Read by AtelSys_Btl_011_AiAddCommand_resi 0x7A44B0, sub_799D50 0x799D50, BtlAi_GetProperty 0x7B2DC0
  /* 0x5CC */ int m_totalAp;   // total AP, written by Btl_LoadCharRecordIntoActor. No reader found
  /* 0x5D0 */ int m_hp;   // current HP. AI property 242
  /* 0x5D4 */ int m_mp;   // current MP. AI property 1
  /* 0x5D8 */ unsigned __int8 m_critBonus;   // critical chance bonus, sum of equip entry +10 over both pieces (FFX_Btl_LoadCharRecordIntoActor 0x79C87A). FFX_Btl_RollCritical 0x7896EE uses it as the base crit chance when the ability flag 0x8 is set [V]
                                             // Written by Btl_LoadCharRecordIntoActor 0x79C5F0. Read by Btl_LoadCharRecordIntoActor 0x79C5F0, FFX_Btl_RollCritical 0x789690
  /* 0x5D9 */ unsigned __int8 m_elemAttack;   // attack element bits (auto-ability row +17, 0 for monsters at 0x79B931), ORed into the ability's element
                                              // by sub_78CD30 0x78CD61 and read by FFX_Btl_ComputeEffectForTarget 0x78E766. The five element
                                              // bytes 0x5D9..0x5DD hold one bit per element. The damage multiplier FFX_Btl_ApplyElementAffinity 0x78A360 reads the
                                              // other four [V 2026-10-28]
  /* 0x5DA */ unsigned __int8 m_elemAbsorb;   // element absorb bits (row +18, monster param +0x2B). It negates the damage (0x78A76C). AI properties 98..102
  /* 0x5DB */ unsigned __int8 m_elemImmune;   // element immunity bits (row +19, monster param +0x2C). It returns 0 (0x78A6E8). AI properties 103..107
  /* 0x5DC */ unsigned __int8 m_elemHalf;   // element half damage bits (row +20, monster param +0x2D). It halves. AI properties 108..112
  /* 0x5DD */ unsigned __int8 m_elemWeak;   // element weakness bits (row +21, monster param +0x2E). It multiplies by 1.5 (0x78A3B6). AI properties 113..117
  /* 0x5DE */ unsigned __int8 m_statusInflict[25];   // weapon-borne status chances, one per status index (auto-ability row +22, BATTLE_CTB_AND_STATUS.md 2.2)
  /* 0x5F7 */ unsigned __int8 m_statusInflictTime[13];   // weapon-borne durations for the 13 counter statuses (row +47)
  /* 0x604 */ unsigned __int16 m_inflictStatus2;   // weapon-borne status 2 mask (row +94)
  /* 0x606 */ unsigned __int16 m_status1;   // status 1 word, bit per status (BATTLE_CTB_AND_STATUS.md 2.1). AI properties 4..7, 37..44
  /* 0x608 */ unsigned __int8 m_statusCount[13];   // status counters: sleep, silence, darkness, shell, protect, reflect, NulTide, NulBlaze, NulShock,
                                                   // NulFrost, regen, haste, slow. AI properties 45..57
  unsigned __int8 _unk_615[0x1];
  /* 0x616 */ unsigned __int16 m_status2;   // status 2 word (BATTLE_CTB_AND_STATUS.md 2.1). AI properties 143..157
  /* 0x618 */ unsigned __int16 m_savedStatus1;   // status 1 saved under the auto-statuses (Btl_RestoreAutoStatuses 0x79B1A0)
  /* 0x61A */ unsigned __int8 m_savedStatusCount[13];   // status counters saved under the auto-statuses
  unsigned __int8 _unk_627[0x1];
  /* 0x628 */ unsigned __int16 m_savedStatus2;   // status 2 saved under the auto-statuses
  /* 0x62A */ unsigned __int16 m_permStatus1;   // permanent status 1 mask
  /* 0x62C */ unsigned __int16 m_permStatusCount;   // permanent counter mask, such a counter is held at 0xFF
  /* 0x62E */ unsigned __int16 m_permStatus2;   // permanent status 2 mask
  /* 0x630 */ unsigned __int16 m_autoStatus[3];   // auto-status masks (status 1, counters, status 2) from the equipment rows (row +88..+92) or the monster
                                                  // params (+72..+76), merged by sub_79B290
  /* 0x636 */ unsigned __int16 m_sosStatus[3];   // the same three masks for the SOS (low HP) auto-statuses, applied while m_sosState is 1 or 2 [I]
  /* 0x63C */ unsigned __int8 m_sosEnabled;   // set by Btl_LoadCharRecordIntoActor, tested by sub_7AF260 0x7AF260
  /* 0x63D */ unsigned __int8 m_weakPose;   // stance level sub_78D7F0 0x78D8B5 derives from the HP band. 0 normal, 1 for sleep, silence, darkness or airborne, 2 for the worst band. Forced 0 by m_weakPoseSuppressed. Debug label WK. AI property 8 via FFX_Btl_GetUnitAiProp8 [I]
                                            // AI property 8 (set). sub_795840 0x795840 returns it
  /* 0x63E */ unsigned __int8 m_sosState;   // SOS state written by sub_78D7F0 0x78D7F0
  /* 0x63F */ unsigned __int8 m_sosStatePrev;   // compared with m_sosState by sub_7AF260
  /* 0x640 */ unsigned __int8 m_derivedFlags;   // derived status flags, bit 4 makes the MP cost 0 (FFX_Btl_GetRawMpCost 0x78D013), on an attacker bit 8 lifts HP amounts 1..9998 to 9999 and -9998..-1 to -9999 (0x78ED07), bit 0x10 makes every crit land (0x789701)
  /* 0x641 */ unsigned __int8 m_statusResist[25];   // per status resistance, 0xFF immune (row +60, monster param +47, AI properties 175..199)
  /* 0x65A */ unsigned __int16 m_status2Immune;   // status 2 immunity mask (row +96, monster param +78, AI properties 200..214)
  /* 0x65C */ unsigned __int8 m_ctb;   // CTB counter, 0 means ready (FFX_Btl_CtbTick, BATTLE_CTB_AND_STATUS.md 1.1). AI property 232
  /* 0x65D */ unsigned __int8 m_ctbBase;   // 3 * tick, the CTB base and reload value
  /* 0x65E */ unsigned __int8 m_cheerLevel;   // Cheer stacks. FFX_Btl_DamageFormula adds it to strength (0x789CD9) and scales the target's damage by 15 - it (0x789D3C). sub_79A190 0x79A1CB zeroes it [V]
                                              // Written by sub_79A190 0x79A190. Read by Btl_DamageFormula 0x789BF0, sub_7DBEE0 0x7DBEE0
  /* 0x65F */ unsigned __int8 m_aimLevel;   // Aim stacks. FFX_Btl_RollHit 0x78A9CE adds 10 * (attacker aim - target reflex) to the hit chance [V]
                                            // No writer found. Read by FFX_Btl_RollHit 0x78A890, sub_7DBEE0 0x7DBEE0
  /* 0x660 */ unsigned __int8 m_focusLevel;   // Focus stacks. FFX_Btl_DamageFormula adds it to magic (0x789E74) and scales the target's damage by 15 - it (0x789E2E) [V]
                                              // No writer found. Read by Btl_DamageFormula 0x789BF0, sub_7DBEE0 0x7DBEE0
  /* 0x661 */ unsigned __int8 m_reflexLevel;   // Reflex stacks. FFX_Btl_RollHit 0x78A9D5 subtracts 10 * the target's value from the hit chance [V]
                                               // No writer found. Read by FFX_Btl_RollHit 0x78A890, sub_7DBEE0 0x7DBEE0
  /* 0x662 */ unsigned __int8 m_luckLevel;   // Luck stacks. Added with the attacker's luck to the hit chance (FFX_Btl_RollHit 0x78A9EC) and the crit chance (FFX_Btl_RollCritical 0x7896B8). sub_79A190 0x79A1D1 zeroes it [V]
                                             // Written by sub_79A190 0x79A190. Read by FFX_Btl_RollHit 0x78A890, sub_7DBEE0 0x7DBEE0, FFX_Btl_RollCritical 0x789690
  /* 0x663 */ unsigned __int8 m_jinxLevel;   // Jinx stacks on the target. Added to the hit chance (FFX_Btl_RollHit 0x78A9F5) and the crit chance (FFX_Btl_RollCritical 0x7896D6) against it, cancelling its luck [V]
                                             // No writer found. Read by FFX_Btl_RollHit 0x78A890, sub_7DBEE0 0x7DBEE0, FFX_Btl_RollCritical 0x789690
  /* 0x664 */ unsigned __int16 m_abilityBits[22];   // usable ability bitfield, bit (id & 0xF) of word (id & 0xFFF) / 16. sub_79BB50 0x79BB50 fills it
                                                    // from the character record words +62..+72 and the save block at 0x11307FC, sub_79C070 0x79C070 sets bits
  /* 0x690 */ unsigned __int16 m_sealedAbilities[22];   // sealed ability bitfield, same indexing (FFX_Btl_IsCommandLocked, sub_79B470 0x79B470)
  /* 0x6BC */ unsigned __int16 m_autoAbility[3];   // the 48 bit auto-ability effect mask (row +98/+100/+102). Word 0 bit 2 First Strike, word 2 bit 2
                                                   // Capture (BATTLE_CTB_AND_STATUS.md 1.1, MONSTER_ARENA.md). AI properties 58..63, 66..71, 74, 76..78
  unsigned __int8 _unk_6C2[0x4];
  /* 0x6C6 */ unsigned __int16 m_forcedCommand;   // forced command id for the forced action paths (sub_799D50 0x799D50). 0x3000 for a character,
                                                  // monster param +112
  /* 0x6C8 */ unsigned __int16 m_escapeCommand;   // this unit's escape command id (FFX_Btl_GetEscapeCommandId 0x78FBA4)
  /* 0x6CA */ unsigned __int8 m_selfKoAfterAction;   // self KO after the action. FFX_Btl_StartCommandExec 0x7AE5F6 sets 1 when ability dword +28 has 0x400000, FFX_Btl_ApplyPendingHits 0x78DA60 then zeroes HP, calls FFX_Btl_OnUnitDefeated on itself and clears it at 0x78DA9B [V]
                                                     // Written by sub_78D980 0x78D980, Btl_StartCommandExec 0x7AE370. Read by sub_78D980 0x78D980
  /* 0x6CB */ unsigned __int8 m_queuedAiCount;   // number of entries in m_queuedAiCommand, at most 4 (sub_7AC840 0x7AC840)
  /* 0x6CC */ unsigned __int8 m_mpCost;   // resolved MP cost, cached by FFX_Btl_CheckCommandCost (AI property 310)
  /* 0x6CD */ unsigned __int8 m_overdriveCost;   // resolved overdrive cost (AI property 311)
  /* 0x6CE */ unsigned __int8 m_noMpCost;   // when set the ability MP cost (record +37) is taken as 0, FFX_Btl_GetRawMpCost 0x78CF8F. Only FFX_BtlAi_SetProperty 0x7B60FC writes it. AI property 258 [V]
                                            // AI property 258. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_GetRawMpCost 0x78CF70
  /* 0x6CF */ unsigned __int8 m_overdriveCostFullGauge;   // one shot: FFX_Btl_LatchOverdriveCost 0x7B0443 sets m_overdriveCost to the gauge max m_overdriveMax and clears the flag at 0x7B0452. Set by FFX_BtlAi_SetProperty 0x7B64A0. AI property 312 [V]
                                                          // AI property 312. Written by BtlAi_SetProperty 0x7B4B70, Btl_LatchOverdriveCost 0x7B03E0. Read by BtlAi_GetProperty 0x7B2DC0, Btl_LatchOverdriveCost 0x7B03E0
  /* 0x6D0 */ unsigned __int8 m_cutCommandOnDefeat;   // set from sub_7AE0F0 on the first action of a command (FFX_Btl_StartCommandExec 0x7AE4BF). If the unit dies while set, FFX_Btl_OnUnitDefeated 0x78C8E0 ends its queued command (count = index + 1). Cleared by FFX_Btl_FinishActions 0x7914E5. AI property 313 [V]
                                                      // AI property 313. Written by Btl_FinishActions 0x7911E0, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_OnUnitDefeated 0x78C740, Btl_StartCommandExec 0x7AE370
  /* 0x6D1 */ unsigned __int8 m_summoner;   // the summoning unit, -1 none (sub_788E10 0x788E10). AI property 262
  /* 0x6D2 */ unsigned __int8 m_regenTicks;   // CTB ticks since the last regen heal, saturates at 0xFF
  /* 0x6D3 */ unsigned __int8 m_coverAttacker;   // the attacker whose hit this unit took for an ally under Guard or Sentinel. sub_78BA70 0x78BBAA writes it with the guard from sub_78EEF0 (status 2 0x3000). sub_7AF920 0x7AF954 turns to face it. 0xFF none, word reset with 0x6D4 by sub_7AF880 [V]
                                                 // Written by Btl_LoadUnitParams 0x79B4F0, sub_78BA70 0x78BA70, sub_7AF880 0x7AF880. Read by sub_7AF920 0x7AF920
  /* 0x6D4 */ unsigned __int8 m_coveredUnit;   // the ally this unit is covering (Guard or Sentinel). sub_78BA70 0x78BBB0 writes it, sub_7AF800 0x7AF819 resets every unit with it set, sub_7AF8A0 0x7AF8B0 waits on both units. 0xFF none (FFX_Btl_SetupUnitRoster 0x79C2EB) [V]
                                               // Written by Btl_SetupUnitRoster 0x79C110, sub_78BA70 0x78BA70. Read by sub_7DBEE0 0x7DBEE0, sub_7AF800 0x7AF800, sub_7AF8A0 0x7AF8A0 and 1 more
  unsigned __int8 _unk_6D5[0x1];
  /* 0x6D6 */ unsigned __int16 m_captureIndex;   // capture species index, 255 not capturable (Btl_OnUnitDefeated, MONSTER_ARENA.md)
  /* 0x6D8 */ unsigned __int8 m_aeonRecovery;   // aeon recovery countdown (Btl_IsUnitUnsummonable 0x79A0A9)
  /* 0x6D9 */ unsigned __int8 m_aeonRecoveryLength;   // aeon recovery length, incremented by Btl_StartAeonRecovery 0x78E0C4
  /* 0x6DA */ unsigned __int8 m_damageReducedByStatus;   // set when Shield (FFX_Btl_ApplyShieldAndBoost 0x78C547) or Defend or Sentinel on a physical hit (FFX_Btl_ApplyDefendAndSentinel 0x78A800) cut the damage. FFX_Btl_PlayUnitReaction 0x78CB9F then swaps reaction event 9 or 48 for 67. Cleared per turn by sub_7AF4B0 0x7AF4CA [V]
                                                         // Written by sub_7AF4B0 0x7AF4B0, FFX_Btl_ApplyShieldAndBoost 0x78C530, FFX_Btl_ApplyDefendAndSentinel 0x78A7D0. Read by sub_78CA20 0x78CA20
  /* 0x6DB */ unsigned __int8 m_actionHitTotal;   // total hits of the last action over all targets (sum of ability record +43 per target, the *a7 out of sub_78BA70). Stored by FFX_Btl_ApplyActionToTargets 0x789598. AI property 246 [V]
                                                  // AI property 246. Written by Btl_ApplyActionToTargets 0x7892E0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0
  /* 0x6DC */ unsigned __int8 m_counterPending;   // the unit will counter the hit being applied. FFX_Btl_CheckCounter 0x78C24E sets it from the counter auto-abilities, FFX_Btl_ApplyPendingEffects 0x78F53D then calls sub_78D8D0, which commits the counter command and clears it at 0x78D95F [V]
                                                  // Written by sub_78D8D0 0x78D8D0, FFX_Btl_CheckCounter 0x78C1D0. Read by Btl_ApplyPendingEffects 0x78F060
  /* 0x6DD */ unsigned __int8 m_useLivePosAsHome;   // when set FFX_Btl_GetUnitPlacementPos 0x79495F returns the live x and z instead of the home point. sub_78D8D0 0x78D907 sets it on an attacker countered after an approach action (+0x446 == 1). Cleared by sub_78BA70, FFX_Btl_OnUnitDefeated, FFX_Btl_ApplyPendingEffects [V]
                                                    // Written by Btl_ApplyPendingEffects 0x78F060, sub_78BA70 0x78BA70, Btl_OnUnitDefeated 0x78C740 and 1 more. Read by maybe_Btl_GetUnitTargetPos 0x7948E0
  /* 0x6DE */ unsigned __int8 m_forceAttack;   // when set the current command is forced to ability 0x3028 (BATTLE_COMMAND.md)
  /* 0x6DF */ unsigned __int8 m_actionCount;   // actions started, FFX_Btl_StartCommandExec 0x7AE423 adds 1 per command clamped 1..255. FFX_Btl_PlaceArrivedAeon 0x7AC715 resets it for an arriving aeon. AI property 276 [V]
                                               // AI property 276. Written by Btl_PlaceArrivedAeon 0x7AC6A0, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_StartCommandExec 0x7AE370
  /* 0x6E0 */ unsigned __int8 m_summonerCuePending;   // one shot set by FFX_BtlAi_SetProperty 0x7B62CE. FFX_Btl_ExecCommand 0x7927C4 then runs battle event 78 on m_summoner (sub_7AFA80), waits in state 6 for +0x432 to clear and clears the flag at 0x7923DB. AI property 277 [V]
                                                      // AI property 277. Written by Btl_ExecCommand 0x792210, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_ExecCommand 0x792210
  /* 0x6E1 */ unsigned __int8 m_motivation;   // 0..100 value, 50 for a character record and 0 for a monster (FFX_Btl_LoadUnitParams 0x79B629, 0x79B923). sub_7AFB60 0x7AFBAA adds a per turn delta, AI property 279 adds clamped. The sub_792F20 debug line prints it for units 15..17, so likely the Magus Sisters motivation [I]
                                              // AI property 279. Written by Btl_LoadUnitParams 0x79B4F0, BtlAi_SetProperty 0x7B4B70, sub_7AFB60 0x7AFB60. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B2B00 0x7B2B00, BtlAi_SetProperty 0x7B4B70 and 1 more
  unsigned __int8 _unk_6E2[0x2];
  /* 0x6E4 */ int m_workHp;   // working HP during damage staging (Btl_CtbPreview_ApplyPendingCmd copies m_hp)
  /* 0x6E8 */ int m_workMp;   // working MP during damage staging
  /* 0x6EC */ int m_workCtb;   // working CTB during damage staging, the current value for CTB damage formulas
  /* 0x6F0 */ int m_overdriveModesCounted;   // per battle bitmask of overdrive modes already counted
  /* 0x6F4 */ int m_overdriveRefDamage;   // reference damage for overdrive gain, max of two self damage formulas (14 and 20), written by FFX_Btl_ComputeOverdriveRefDamage 0x78D7D3. Divisor in FFX_Btl_GainOverdriveOnHit 0x7B0EAD and sub_7B0F80 0x7B102A. Debug label DMG [V]
                                          // Written by Btl_ComputeOverdriveRefDamage 0x78D790. Read by Btl_GainOverdriveOnHit 0x7B0D50, sub_7B0F80 0x7B0F80, sub_7DBEE0 0x7DBEE0
  /* 0x6F8 */ int m_statusFxMask;   // current status visual mask (FFX_Btl_GetUnitStatusFxMask), stored each refresh by sub_79EFF0 0x79F0DB [V]
                                    // Written by sub_79EFF0 0x79EFF0. No reader found
  /* 0x6FC */ int m_appliedStatusFxMask;   // status visual mask last pushed to the CHR. sub_79EFF0 0x79F0E1 calls sub_7FAED0 only when the new mask differs, then stores it. -1 forces a refresh (FFX_Btl_LoadUnitParams 0x79B618, FFX_Btl_OnUnitChrReloaded 0x783BEF) [V]
                                           // Written by Btl_LoadUnitParams 0x79B4F0, Btl_OnUnitChrReloaded 0x783BC0, sub_79EFF0 0x79EFF0. Read by sub_79EFF0 0x79EFF0
  /* 0x700 */ unsigned __int8 m_defeatStyle;   // how the unit was defeated, latched by FFX_Btl_OnUnitDefeated 0x78C7C5: m_pendingDefeatStyle if set, else 1 for an Eject death, else 0. Cleared by FFX_Btl_ResetUnitForReentry 0x78D592. AI property 294, debug label DT [V]
                                               // AI property 294. Written by Btl_OnUnitDefeated 0x78C740, BtlAi_SetProperty 0x7B4B70, Btl_ResetUnitForReentry 0x78D530. Read by BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0
  /* 0x701 */ unsigned __int8 m_pendingDefeatStyle;   // defeat style the killing hit asks for: 2 bribed (FFX_Btl_RollBribe 0x78BE9B), 3 KO from ability 0x30E2 and 4 KO from 0x3120 (FFX_Btl_RollStatus1AndCounters 0x78B080). FFX_Btl_PayKillRewards 0x799160 pays bribe spoils on 2, sub_79E3D0 skips the death effect on 3 [V]
                                                      // Written by Btl_RollStatus1AndCounters 0x78AE00, Btl_RollBribe 0x78BDD0. Read by Btl_PayKillRewards 0x7990D0, Btl_OnUnitDefeated 0x78C740, sub_79E3D0 0x79E3D0
  /* 0x702 */ unsigned __int8 m_defeatCause;   // 1 HP reached 0, 2 Eject (status 2 0x100), written by FFX_Btl_OnUnitDefeated 0x78C7BC. FFX_Btl_EndPhaseStep 0x791879 plays the aeon KO transition only for cause 1 [V]
                                               // Written by Btl_OnUnitDefeated 0x78C740. Read by Btl_EndPhaseStep 0x7917D0
  unsigned __int8 _unk_703[0x1];
  /* 0x704 */ int m_bribeGilTotal;   // gil offered to this unit by Bribe so far, accumulated by FFX_Btl_RollBribe 0x78BEE8 (clamped to 999999999) and fed into the bribe ratio at 0x78BDED [V]
                                     // Written by Btl_RollBribe 0x78BDD0. Read by Btl_RollBribe 0x78BDD0
  /* 0x708 */ int m_bribeRatio;   // bribe ratio ((gil << 8) / max HP) / 20 - 64 stored on success by FFX_Btl_RollBribe 0x78BEA2, its square root scales the item count in FFX_Btl_PayKillRewards 0x79919A [V]
                                  // Written by Btl_RollBribe 0x78BDD0. Read by Btl_PayKillRewards 0x7990D0
  /* 0x70C */ float m_overdriveRate;   // overdrive gain multiplier, 1.0 at setup (FFX_Btl_AddOverdrive 0x7B1681)
  unsigned __int8 _unk_710[0x2];
  /* 0x712 */ unsigned __int16 m_gilStealChance;   // chance out of 255 that a gil steal works (ability record +33 bit 0), rolled on stream 10 by sub_78B920 0x78B95C and halved after each success (0x78BA04, min 1). 255 at load (FFX_Btl_LoadUnitParams 0x79B659). Read as signed 16 bit [V]
                                                   // Written by Btl_LoadUnitParams 0x79B4F0, sub_78B920 0x78B920. Read by sub_78B920 0x78B920
  /* 0x714 */ unsigned __int8 m_turnDeltasEnabled;   // enables the per turn deltas at +0x726..+0x729, sub_7AFB60 0x7AFB67 applies them only when set. Written 1 by FFX_AtelSys_Btl_253_SetCmdRange714_resi 0x7A3A56 and FFX_AtelSys_Btl_254_SetCmdRange714Ex_resi 0x7A3BC9 as a word, which also clears +0x715 [V]
                                                     // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. Read by sub_7AFB60 0x7AFB60
  /* 0x715 */ unsigned __int8 m_actionHadEffect;   // set on the attacker by FFX_Btl_MarkActionHadEffect 0x78BFA3 when its action dealt damage (or healed for a heal flagged ability, record +32 bit 0x10). sub_7AFB60 0x7AFB78 uses bit 0 to pick delta 0 or 1 at turn end [V]
                                                   // Written by FFX_Btl_MarkActionHadEffect 0x78BF50. Read by sub_7AFB60 0x7AFB60
  /* 0x716 */ unsigned __int8 m_turnReoffered;   // the turn was handed back after a rejected command (FFX_Btl_ExecCommand 0x792761)
  /* 0x717 */ unsigned __int8 m_mixResolved;   // the Mix items were consumed and the result written into the command, set by sub_7B0520 0x7B0692 (the Mix prepare step of FFX_Btl_ExecCommand). Cleared by FFX_Btl_CmdQueue_RemoveAt 0x7B08FA. FFX_Btl_ExecCommand 0x792782 shows message 12311 on a failed mixed command [V]
                                               // Written by Btl_CmdQueue_RemoveAt 0x7B0860, sub_7B0520 0x7B0520. Read by Btl_ExecCommand 0x792210, sub_7B0520 0x7B0520
  /* 0x718 */ unsigned __int8 m_b718;   // AI property 137. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_795250 0x795250, sub_7DBEE0 0x7DBEE0
  /* 0x719 */ unsigned __int8 m_b719;   // AI property 136. Written by Btl_LoadUnitParams 0x79B4F0, sub_793E00 0x793E00, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_795250 0x795250, sub_7DBEE0 0x7DBEE0
  /* 0x71A */ unsigned __int16 m_queuedAiCommand[4];   // command ids queued for the unit by sub_7AC840 0x7AC840
  /* 0x722 */ unsigned __int8 m_queuedAiPriority[4];   // priority 0..7 of each m_queuedAiCommand
  /* 0x726 */ char m_motivationDeltaNoEffect;   // signed per turn delta added to m_motivation by sub_7AFB60 0x7AFBA2 when m_actionHadEffect is 0. Written by btl 253 0x7A3A5F and btl 254 0x7A3BD5. Read as char, so signed [V]
                                                // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. Read by sub_7AFB60 0x7AFB60
  /* 0x727 */ char m_motivationDeltaEffect;   // signed per turn delta added to m_motivation by sub_7AFB60 (index 1 of +0x726) when m_actionHadEffect is 1. Written by btl 253 0x7A3A6B and btl 254 0x7A3BDE. Signed like +0x726 [V]
                                              // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. No reader found
  /* 0x728 */ char m_overdriveDeltaNoEffect;   // signed per turn delta added to the overdrive gauge (clamped to m_overdriveMax) by sub_7AFB60 0x7AFB8C when m_actionHadEffect is 0. Written by btl 253 0x7A3A65 and btl 254 0x7A3BEA. Read as char [V]
                                               // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. Read by sub_7AFB60 0x7AFB60
  /* 0x729 */ char m_overdriveDeltaEffect;   // signed per turn overdrive gauge delta (index 1 of +0x728) used by sub_7AFB60 when m_actionHadEffect is 1. Written by btl 253 0x7A3A71 and btl 254 0x7A3BE4 [V]
                                             // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. No reader found
  unsigned __int8 _unk_72A[0x2];
  /* 0x72C */ unsigned __int8 m_command[72];   // the unit's current 72 byte command record, same layout as the command queue (BATTLE_COMMAND.md).
                                               // FFX_Btl_ExecCommand 0x792803 copies the finished record here, FFX_Btl_FinishActions clears it,
                                               // FFX_Btl_CmdQueue_GetActorCmdOrLast 0x7B09B0 falls back to it
  /* 0x774 */ BTLHITLOG m_hitLog[2];   // the two pending effect blocks, staged by FFX_Btl_ComputeEffectForTarget and applied by
                                       // FFX_Btl_ApplyPendingEffects 0x78F060, which walks both
  /* 0xD24 */ unsigned __int8 m_odResultPending;   // minigame overdrive result latch, BtlOd_ReportMinigameResult 0x7B0489
  unsigned __int8 _unk_D25[0x1];
  /* 0xD26 */ unsigned __int8 m_odMinigameActive;   // set by sub_7AFE00 0x7AFEE9 when an overdrive minigame starts
  unsigned __int8 _unk_D27[0x1];
  /* 0xD28 */ int m_odResult[3];   // the three values BtlOd_ReportMinigameResult stores
  /* 0xD34 */ int m_hpDamageSum;   // HP damage accumulated this action (FFX_Btl_ComputeEffectForTarget, AI property 166)
  /* 0xD38 */ int m_mpDamageSum;   // MP damage accumulated (AI property 167)
  /* 0xD3C */ int m_ctbDamageSum;   // CTB damage accumulated (AI property 168)
  /* 0xD40 */ unsigned __int8 m_targetOrder[31];   // the targets of the current command in party order, 0xFF padded. Built by FFX_Btl_BuildTargetOrder 0x7B7090
                                                   // (magic host API 218), also the buffer FFX_BtlOd_WakkaReels_GetStrip 0x7B1910 returns
  unsigned __int8 _unk_D5F[0x1];
  /* 0xD60 */ unsigned __int8 m_targetOrderEx[31];   // m_targetOrder plus the units sub_7B7440 adds, built by sub_7B6FB0 0x7B6FB0 (magic host API 368)
  unsigned __int8 _unk_D7F[0x1];
  /* 0xD80 */ unsigned __int8 m_reelSymbols[64];   // Wakka's reel strips back to back, written by sub_7B1B50 0x7B1B50 and sub_7B7170 0x7B7170, read in
                                                   // pieces by FFX_BtlOd_WakkaReels_GetStrip using the strip lengths from sub_7B19D0
  /* 0xDC0 */ int m_targetMask;   // target mask of the running action, BtlAi_SetLastPickMask 0x796458. AI property 298
  /* 0xDC4 */ int m_prevTargetMask;   // m_targetMask saved by FFX_Btl_ExecCommand 0x79265C
  /* 0xDC8 */ unsigned __int8 m_onField;   // present in the fight. AI property 21
  /* 0xDC9 */ unsigned __int8 m_onFieldSaved;   // saved copy of m_onField. FFX_Btl_BeginSummon 0x7ADFB0 copies it before a summon, FFX_Btl_SwapPartyArrangement 0x7ADEFA swaps it with m_onField for units 0..17 [V]
                                                // Written by Btl_SwapPartyArrangement 0x7ADE10, Btl_BeginSummon 0x7ADF90. Read by Btl_SwapPartyArrangement 0x7ADE10
  /* 0xDCA */ unsigned __int8 m_countsAsOnField;   // lets a unit that is not on the field still count as present: the unit lists test (m_onField || this), FFX_Btl_BuildUnitListByKind 0x7B6D00, sub_785410 0x7855E6, sub_793CF0 0x793D23. Only FFX_BtlAi_SetProperty 0x7B638E writes it. AI property 295 [V]
                                                   // AI property 295. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_785410 0x785410, sub_7B6C50 0x7B6C50, sub_793CF0 0x793CF0 and 1 more
  /* 0xDCB */ unsigned __int8 m_offFieldAsSummoner;   // the summoner standing aside while its aeon is out, set by FFX_Btl_SwapUnitIntoPartySlot 0x7ADB88 on the summon path, cleared on recall 0x7ADD70. Blocks the poison tick in FFX_Btl_TurnEndStatusTick 0x7AF3F8 [V]
                                                      // Written by Btl_SwapUnitIntoPartySlot 0x7ADAE0. Read by Btl_TurnEndStatusTick 0x7AF390, sub_7AAAE0 0x7AAAE0
  /* 0xDCC */ unsigned __int8 m_ko;   // KO. AI property 332
  /* 0xDCD */ unsigned __int8 m_leftBattle;   // the unit left the battle (escape and similar), set with m_onField = 0 by FFX_Btl_SetUnitLeftBattle 0x78DE4C. Present means m_onField && !this (FFX_BtlAi_ResolveTargetSelector 0x794362), banks no AP (FFX_Btl_BankKillAp 0x798A06). AI property 173, debug ES [V]
                                              // AI property 173. Written by Btl_SetUnitLeftBattle 0x78DE40, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_ResolveTargetSelector 0x794330, sub_785410 0x785410, sub_7B6C50 0x7B6C50 and 10 more
  /* 0xDCE */ unsigned __int8 m_petrified;   // mirror of status 1 bit 4 (Petrify), rewritten by FFX_Btl_ApplyPendingEffects
  /* 0xDCF */ unsigned __int8 m_leftPetrifiedAtDefeat;   // set by FFX_Btl_OnUnitDefeated 0x78C7EA when the unit had left the battle and was petrified. sub_793E00 0x793EB1 then skips the walk back to its home spot for a petrified unit [V]
                                                         // Written by Btl_OnUnitDefeated 0x78C740. Read by sub_793E00 0x793E00
  /* 0xDD0 */ unsigned __int8 m_capturePending;   // capture pending, then the result (Btl_OnUnitDefeated)
  /* 0xDD1 */ unsigned __int8 m_captureResult;   // capture result kept for the AI getter. AI property 342
  /* 0xDD2 */ unsigned __int8 m_countsForBattleEnd;   // the unit counts in FFX_Btl_TestBattleOver, party alive check 0x7928F7 and enemy all down check 0x792962, and gets the victory pose in FFX_Btl_EndPhaseStep 0x791CF5. 1 at roster setup (word write 0x79C1B1). AI property 142 [V]
                                                      // AI property 142. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by Btl_TestBattleOver 0x7928D0, BtlAi_GetProperty 0x7B2DC0, Btl_EndPhaseStep 0x7917D0
  /* 0xDD3 */ unsigned __int8 m_countsAsPartyAlive;   // second gate for a party unit to count as standing in FFX_Btl_TestBattleOver 0x792900, needs m_countsForBattleEnd too. 1 at roster setup by the word write at 0x79C1B1. AI property 172 [V]
                                                      // AI property 172. Written by BtlAi_SetProperty 0x7B4B70. Read by Btl_TestBattleOver 0x7928D0, BtlAi_GetProperty 0x7B2DC0
  /* 0xDD4 */ unsigned __int8 m_summonAllowed;   // 0 locks the unit as an aeon command, FFX_Btl_IsUnitUnsummonable 0x79A097. 1 at roster setup 0x79C1BA, script btl 059 writes it through sub_79B470 0x79B4DC for an id with no top nibble [V]
                                                 // Written by sub_79B470 0x79B470, Btl_SetupUnitRoster 0x79C110. Read by Btl_IsUnitUnsummonable 0x79A080
  /* 0xDD5 */ unsigned __int8 m_bDD5;   // AI property 344. Written by Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_145_resi 0x7A77C0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, AtelSys_Btl_145_resi 0x7A77C0
  /* 0xDD6 */ unsigned __int8 m_takesTurns;   // takes CTB turns. AI property 81
  /* 0xDD7 */ unsigned __int8 m_inTurnStrip;   // shown in the turn order strip. AI property 83
  /* 0xDD8 */ unsigned __int8 m_bDD8;   // AI property 215. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by sub_785410 0x785410, BtlAi_GetProperty 0x7B2DC0
  /* 0xDD9 */ unsigned __int8 m_targetable;   // the unit can be targeted: sub_799610 0x79964E counts a target valid only with m_onField and this set, FFX_Btl_BuildUnitListByKind kind 19 0x7B6DD5 lists enemy slots with it, btl 16 filters on it. 1 at roster setup 0x79C449. AI property 82, debug CS [V]
                                              // AI property 82. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by sub_7B6C50 0x7B6C50, sub_794CA0 0x794CA0, BtlAi_GetProperty 0x7B2DC0 and 2 more
  /* 0xDDA */ unsigned __int8 m_inAimCentre;   // for a multi unit target mask sub_787900 0x78792B keeps only units with this set when computing the aim and facing point (sub_7969F0, sub_7AA050). 1 at roster setup by the word write at 0x79C449. AI property 225 (FFX_Btl_GetUnitAiProp225) [V]
                                               // AI property 225. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_787C10 0x787C10, sub_787900 0x787900, BtlAi_GetProperty 0x7B2DC0
  /* 0xDDB */ unsigned __int8 m_bDDB;   // AI property 337. Written by BtlAi_SetProperty 0x7B4B70. Read by Btl_TurnStartStatusTick 0x7AF4F0, BtlAi_GetProperty 0x7B2DC0
  unsigned __int8 _unk_DDC[0x1];
  /* 0xDDD */ unsigned __int8 m_bDDD;   // AI property 80. Written by AtelSys_Btl_079_resi 0x7A70C0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_794F20 0x794F20, sub_7DBEE0 0x7DBEE0
  /* 0xDDE */ unsigned __int16 m_wDDE;   // AI property 345. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_794F20 0x794F20
  unsigned __int8 _unk_DE0[0x4];
  /* 0xDE4 */ unsigned __int8 m_turnQueueIndex;   // index of this unit's turn queue entry, 0xFF none
  /* 0xDE5 */ unsigned __int8 m_cmdQueueIndex;   // index of this unit's command queue entry (FFX_Btl_CmdQueue_RemoveAt)
  /* 0xDE6 */ unsigned __int8 m_turnQueueCount;   // number of turn queue entries for this unit
  /* 0xDE7 */ unsigned __int8 m_cmdQueueCount;   // number of command queue entries for this unit
  /* 0xDE8 */ unsigned __int8 m_actionRank;   // rank of the current action (FFX_Btl_ApplyActionToTargets 0x78934C, reset to 3 by FFX_Btl_CtbTick)
  /* 0xDE9 */ unsigned __int8 m_turnRetirePending;   // set 1 when FFX_Btl_ExecCommand 0x7924CF starts the action. FFX_Btl_ApplyPendingHits 0x78DA24 uses it on the last sub action with no hit to retire the turn queue entry once, then clears it. FFX_Btl_FinishActions 0x7914AD clears it [V]
                                                     // Written by sub_78D980 0x78D980, Btl_FinishActions 0x7911E0, Btl_ExecCommand 0x792210. Read by sub_78D980 0x78D980
  /* 0xDEA */ unsigned __int8 m_postActionKind;   // post action kind, copied from command record +6
  /* 0xDEB */ unsigned __int8 m_timesStolenFrom;   // successful steals taken from this unit, FFX_Btl_ApplySteal 0x78B8CF adds 1 up to 0xFF. AI property 121 [V]
                                                   // AI property 121. Written by BtlAi_SetProperty 0x7B4B70, sub_78B760 0x78B760. Read by BtlAi_GetProperty 0x7B2DC0, sub_78B760 0x78B760
  /* 0xDEC */ unsigned __int8 m_predictedDeath;   // predicted to die from the staged action. AI property 27
  /* 0xDED */ unsigned __int8 m_issuer;   // the unit that issued the current command (FFX_Btl_ApplyPendingEffects)
  /* 0xDEE */ unsigned __int8 m_killer;   // the unit that defeated this one, FFX_Btl_OnUnitDefeated 0x78C7CB. FFX_Btl_EndPhaseStep 0x791882 passes it to FFX_Btl_QueueAeonTransition for the aeon KO [V]
                                          // Written by Btl_OnUnitDefeated 0x78C740. Read by Btl_EndPhaseStep 0x7917D0
  /* 0xDEF */ unsigned __int8 m_reactionSource;   // copy of m_issuer taken when FFX_Btl_PlayUnitReaction 0x78CAC1 plays the normal hit reaction (kind 1). No reader found [V]
                                                  // Written by sub_78CA20 0x78CA20. No reader found
  /* 0xDF0 */ unsigned __int8 m_actionState;   // action state: 0 idle, 1 finishing, 2 acting, 3 done, 4 and 5 waiting (BATTLE_COMMAND.md)
  /* 0xDF1 */ unsigned __int8 m_curCmdVariant;   // variant byte of the executing command (record +1, 0 normal turn, 1 counter or extra turn), FFX_Btl_ExecCommand 0x7924D6. Non zero blocks a counter to it (FFX_Btl_CheckCounter 0x78C1E9, btl 224 CanCounter 0x7A849E, FFX_Btl_RunAiAndCommit 0x7AD050). Debug MD [V]
                                                 // Written by Btl_ExecCommand 0x792210. Read by AtelSys_Btl_224_resi 0x7A8450, AtelSys_Btl_225_resi 0x7A85A0, Btl_RunAiAndCommit 0x7ACEB0 and 2 more
  /* 0xDF2 */ unsigned __int8 m_actionHeldByEvent;   // a unit battle event holds the action open: sub_7AB360 0x7AB3D1 sets it when it starts a tracked event, FFX_Btl_FinishActions 0x791231 will not finish while set, btl 000 0x7A8641 releases it. FFX_Btl_StartCommandExec returns it. Debug RQ [V]
                                                     // Written by sub_7AB360 0x7AB360, Btl_QueueAeonTransition 0x7AE1A0, Btl_ExecCommand 0x792210 and 3 more. Read by Btl_FinishActions 0x7911E0, Btl_StartCommandExec 0x7AE370, sub_7DBEE0 0x7DBEE0
  /* 0xDF3 */ unsigned __int8 m_aiControlled;   // the unit is driven by its AI, not the menu. FFX_Btl_SetupUnitRoster 0x79C59C sets it on enemy units, FFX_Btl_IsActorAiControlled 0x792120 returns it (BATTLE_COMMAND.md) [V]
                                                // Written by Btl_SetupUnitRoster 0x79C110. No reader found
  /* 0xDF4 */ unsigned __int8 m_aiFilledCommand;   // the command record was filled by the AI (placeholder with no actions, FFX_Btl_ExecCommand 0x792550, or an aeon special in FFX_Btl_AeonSpecialCommandHook 0x7B2A29). When set a rejected command does not hand the turn back (FFX_Btl_ExecCommand 0x792729) [V]
                                                   // Written by Btl_ExecCommand 0x792210, Btl_AeonSpecialCommandHook 0x7B2980. Read by Btl_ExecCommand 0x792210
  /* 0xDF5 */ unsigned __int8 m_bindOrder;   // order in which the monster slot was bound, g_ffxBtlMonsterBindCounter at FFX_Btl_BindMonsterArchiveSections 0x783B75. FFX_BtlEv_StartFirstMatch 0x797E18 puts it in the thread record (MONSTER_AI.md) [V]
                                             // Written by Btl_BindMonsterArchiveSections 0x783B00. Read by BtlEv_StartFirstMatch 0x797D50
  /* 0xDF6 */ unsigned __int8 m_atelWorkerCount;   // ATEL worker count of the monster, byte 0 of its event map (FFX_Btl_BindMonsterArchiveSections 0x783B6A). FFX_BtlEv_FindSlotRecord 0x798638 sums it over earlier units for the global actor base (MONSTER_AI.md) [V]
                                                   // Written by Btl_BindMonsterArchiveSections 0x783B00. Read by BtlEv_FindSlotRecord 0x798590
  /* 0xDF7 */ unsigned __int8 m_turnEventRan;   // the turn start AI event already ran for this turn (FFX_Btl_SendMenu)
  /* 0xDF8 */ unsigned __int8 m_switchedOut;   // set when the unit leaves the field through FFX_Btl_SwapUnitIntoPartySlot 0x7ADB53 (switch or summon), cleared when it comes in 0x7ADD38 and by FFX_Btl_ApplyPoisonDamage 0x7AFABB. Suppresses the poison tick in FFX_Btl_TurnQueue_RemoveAt 0x7B2200 [V]
                                               // Written by Btl_SwapUnitIntoPartySlot 0x7ADAE0, Btl_ApplyPoisonDamage 0x7AFAB0. Read by Btl_TurnQueue_RemoveAt 0x7B20E0
  /* 0xDF9 */ unsigned __int8 m_markerBone;   // bone index used as the unit screen marker point, FFX_Ch_GetBoneWorldPos in maybe_FFX_BtlUi_CacheUnitBoneHeight 0x7932E6 and sub_79F380 0x79F412. 1 at roster setup 0x79C1F4. AI property 135 [V]
                                              // AI property 135. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7932B0 0x7932B0, sub_79F380 0x79F380
  /* 0xDFA */ unsigned __int8 m_bDFA;   // No writer found. Read by sub_7DBEE0 0x7DBEE0
  /* 0xDFB */ unsigned __int8 m_effectState;   // battle effect state machine (0 idle, 1 begin, 2 cast motion, 3 wait, 4 running, 5 ready, 6 done, 7..16 other paths), switched on by FFX_Btl_StepMagicEffectStates 0x7884AF, set 1 by FFX_Btl_BeginMagicEffect 0x787FBF (EFFECT_OVERLAY.md 3). Debug EF [V]
                                               // Written by sub_788480 0x788480, Btl_BeginMagicEffect 0x787F20, sub_7AD820 0x7AD820 and 8 more. Read by sub_788480 0x788480, sub_787810 0x787810, sub_7AD820 0x7AD820 and 11 more
  /* 0xDFC */ unsigned __int8 m_standInCastPending;   // set to 1 by FFX_Btl_StartCommandExec 0x7AE999 for animation kind 22. Effect state 1 in FFX_Btl_StepMagicEffectStates 0x7885A1 clears it and runs unit event 8 on the weapon stand-in unit (28, 29, 30). FFX_Btl_OnUnitChrReloaded 0x783BE8 clears it [V]
                                                      // Written by sub_788480 0x788480, Btl_StartCommandExec 0x7AE370, Btl_OnUnitChrReloaded 0x783BC0. Read by sub_788480 0x788480
  /* 0xDFD */ unsigned __int8 m_assetLoadPending;   // 1 while an async load for the unit is in flight: magic PRX (FFX_Btl_BeginMagicEffect 0x78805C, cleared by FFX_Btl_StartMagicPrx 0x788169), weapon chr swap (sub_7AD820), Mix prepare.bin (0x7AFC1D, cleared by sub_7B0500). Effect state 3 waits on it 0x788633. Debug label Rd [V]
                                                    // Written by Btl_StartMagicPrx 0x788110, Btl_BeginMagicEffect 0x787F20, sub_7AD820 0x7AD820 and 4 more. Read by sub_788480 0x788480, Btl_PumpChrLoadQueue 0x7AD190, sub_792F20 0x792F20 and 1 more
  /* 0xDFE */ unsigned __int8 m_followEventResult;   // return of the follow ability event FFX_BtlEv_FireAbilityEvent stored by sub_788F30 0x788F6D. FFX_Btl_FreeActorMagicEffect 0x787B31 resets BtlEv group 2 workers 0..2 when it is 2, then clears it. BeginMagicEffect 0x787F98 zeroes it [V] for the use, meaning of 2 [I]
                                                     // Written by sub_787AF0 0x787AF0, Btl_BeginMagicEffect 0x787F20, sub_788F30 0x788F30. Read by sub_787AF0 0x787AF0
  /* 0xDFF */ unsigned __int8 m_sentOffField;   // 1 while host 636 maybe_FFX_Btl_SendUnitOffField 0x7B7E60 has shifted m_placeGroup by +3, cleared by host 635 maybe_FFX_Btl_ReturnUnitToField 0x7B7DAF which shifts it back and re-places the unit. Each tests it first so the shift is not applied twice [V]
                                                // Written by sub_7B7D70 0x7B7D70, sub_7B7E20 0x7B7E20. Read by sub_7B7D70 0x7B7D70, sub_7B7E20 0x7B7E20
  /* 0xE00 */ unsigned __int8 m_startMotionEvent;   // first of 4 bytes 0xE00..0xE03 that FFX_Btl_StartCommandExec sets to 0xFF then fills per animation kind (0x7AE7BE etc). Unit battle event run through sub_7AB360 when the action starts: effect state 1 0x788575 (0xFF skips to state 3) and sub_787D70 0x787DA1 [V]
                                                    // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788480 0x788480, sub_787D70 0x787D70
  /* 0xE01 */ unsigned __int8 m_followMotionEvent;   // second byte of the StartCommandExec motion pair (0x7AE89E, also the high byte of the word writes). Run through sub_7AB360 by FFX_Btl_StartEffectReactions 0x788FA5 when the effect is ready, and by host 529 maybe_FFX_Btl_EffectReturnToIdle 0x787C4E. 0xFF none [V]
                                                     // Written by Btl_StartCommandExec 0x7AE370. Read by sub_787C30 0x787C30, sub_788F90 0x788F90
  unsigned __int8 _unk_E02[0x2];
  /* 0xE04 */ unsigned __int16 m_startAbilityEvent;   // first of 4 words 0xE04..0xE0B set to 0xFFFF then filled by FFX_Btl_StartCommandExec (0x7AE708 etc). Event kind fired with m_startMotionEvent: FFX_BtlEv_FireEvent in effect state 1 0x78861B when an overlay exists, FFX_BtlEv_FireAbilityEvent in sub_787D70 0x787DD3 [V]
                                                      // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788480 0x788480, sub_787D70 0x787D70
  /* 0xE06 */ unsigned __int16 m_followAbilityEvent;   // event kind FFX_BtlEv_FireAbilityEvent fires in sub_788F30 0x788F50, written by FFX_Btl_StartCommandExec 0x7AE764. Fired at effect ready or after the hits per m_followEventEarly. Usually the kind after m_startAbilityEvent (27/52, 44/45, 59/60) [V]
                                                       // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788F30 0x788F30
  /* 0xE08 */ unsigned __int16 m_startAbilityEvent2;   // second kind argument passed with m_startAbilityEvent to FFX_BtlEv_FireEvent 0x78860B and FireAbilityEvent in sub_787D70 0x787DC0. StartCommandExec only ever writes 26 (0x7AE70F, attack kinds 1 and 30) or leaves 0xFFFF [V] for the use, meaning of the second kind [I]
                                                       // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788480 0x788480, sub_787D70 0x787D70
  /* 0xE0A */ unsigned __int16 m_followAbilityEvent2;   // second kind argument with m_followAbilityEvent in sub_788F30 0x788F40. Only written by the 0xFFFF init loop of FFX_Btl_StartCommandExec over 0xE04..0xE0B, so always 0xFFFF in practice [V]
                                                        // No writer found. Read by sub_788F30 0x788F30
  /* 0xE0C */ unsigned __int8 m_bE0C;   // AI property 128. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_794E80 0x794E80
  /* 0xE0D */ unsigned __int8 m_effectHitsDone;   // cleared by FFX_Btl_BeginMagicEffect 0x787F9F, set to 1 by FFX_Btl_StepEffectReactions 0x7890CB/0x789144 when every target reaction is finished. Returned to the DLL by host 227 FFX_Btl_GetEffectHitsDone 0x787CD3. Debug label MT [V]
                                                  // Written by sub_789030 0x789030, Btl_BeginMagicEffect 0x787F20. Read by sub_789030 0x789030, maybe_Btl_GetUnitEffectSlot 0x787CA0, sub_7DBEE0 0x7DBEE0
  /* 0xE0E */ unsigned __int8 m_hitEffectId;   // AI property 170, default 8 (word 264 at 0x79C20B). On a hit motion event sub_7B7680 0x7B7730 plays it as a kind 8 hit effect via sub_79EBA0 when m_hitEffectArmed is set and the hit result has a hit bit. 0 plays none [V]
                                               // AI property 170. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B7680 0x7B7680
  /* 0xE0F */ unsigned __int8 m_prepFxAllowed;   // gates the Btl 064 effect in sub_79E8C0 0x79E8EB (also needs m_prepFxHandle 0). Default 1 at roster setup, set 1 by FFX_Btl_FinishActions 0x7914BD, cleared by FFX_Btl_StartCommandExec 0x7AE606 unless ability dword +28 bit 0x4000000 [V]
                                                 // Written by Btl_FinishActions 0x7911E0, Btl_StartCommandExec 0x7AE370. Read by sub_79E8C0 0x79E8C0
  /* 0xE10 */ unsigned __int8 m_prepFxGround;   // AI property 226. Effect id sub_79E8C0 0x79E921 plays on a ground field. sub_79E890 0x79E8A5 sets 30 for party, 55 for enemies. That effect is stopped when the magic effect starts (state 11) and at FinishActions [V]
                                                // AI property 226. Written by sub_79E890 0x79E890, BtlAi_SetProperty 0x7B4B70. Read by sub_79E8C0 0x79E8C0, BtlAi_GetProperty 0x7B2DC0
  /* 0xE11 */ unsigned __int8 m_prepFxAirborne;   // AI property 227. Effect id sub_79E8C0 0x79E93A plays instead of m_prepFxGround when g_ffxBtlFieldAirborne or unit +0x505 is set. sub_79E890 sets 31 for party, 56 for enemies [V]
                                                  // AI property 227. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_79E8C0 0x79E8C0, BtlAi_GetProperty 0x7B2DC0
  /* 0xE12 */ unsigned __int8 m_effectACount;   // kind 7 effects in flight. ++ by Btl 023 0x7A4093, Btl 211 0x7A4AED and sub_79ECA0 0x79ECE0 on success. Btl 081/188 wait for 0 (0x7A58A9), Btl 209 returns it. Debug label NM [V]
                                                // Written by AtelSys_Btl_023_resi 0x7A4050, sub_79ECA0 0x79ECA0, AtelSys_Btl_211_resi 0x7A4A90. Read by AtelSys_Btl_023_resi 0x7A4050, AtelSys_Btl_081_poll 0x7A5890, sub_79ECA0 0x79ECA0 and 4 more
  /* 0xE13 */ unsigned __int8 m_hitEffectCount;   // kind 8 hit effects in flight. ++ by sub_79EBA0 0x79EBC7 (the m_hitEffectId path, honours g_ffxBtlDbgTestHitEff) and sub_79EC40 0x79EC80. FFX_Btl_TryBeginActionFinish 0x7916A3 waits for 0, Btl 099/189 too. Debug labels HT and HIT [V]
                                                  // Written by sub_79EC40 0x79EC40, sub_79EBA0 0x79EBA0. Read by sub_7DBEE0 0x7DBEE0, AtelSys_Btl_189_poll 0x7A6030, sub_79EC40 0x79EC40 and 4 more
  /* 0xE14 */ unsigned __int8 m_followEventEarly;   // ability word +26 bit 0x4000, set by FFX_Btl_StartCommandExec 0x7AE469. When set FFX_Btl_StartEffectReactions 0x789001 fires the follow ability event at effect ready, else FFX_Btl_StepEffectReactions 0x7890C4 fires it once all hits are done [V]
                                                    // Written by Btl_StartCommandExec 0x7AE370. Read by sub_789030 0x789030, sub_788F90 0x788F90
  /* 0xE15 */ unsigned __int8 m_chrEffectAllowed;   // 1 for every unit at FFX_Btl_SetupUnitRoster 0x79C21E, 0 for summon units 0x79C51C. Btl 131 0x7A42D4 attaches a CHR effect only when it is set and m_chrEffectHandle is 0 [V]
                                                    // Written by Btl_SetupUnitRoster 0x79C110. Read by AtelSys_Btl_131_resi 0x7A42A0
  /* 0xE16 */ unsigned __int8 m_hitEffectArmed;   // set 1 by FFX_Btl_StartCommandExec 0x7AE43A. sub_7B7680 0x7B7727 plays m_hitEffectId on hit motion events only while set, and motion event 9 clears it 0x7B7772 [V]
                                                  // Written by Btl_StartCommandExec 0x7AE370, sub_7B7680 0x7B7680. Read by sub_7B7680 0x7B7680
  /* 0xE17 */ unsigned __int8 m_bE17;   // Written by sub_79E3D0 0x79E3D0, Btl_ResetUnitForReentry 0x78D530. Read by AtelSys_Btl_261_poll 0x7A6730
  unsigned __int8 _unk_E18[0x1];
  /* 0xE19 */ unsigned __int8 m_hitSeArmed;   // set 1 by FFX_Btl_StartCommandExec 0x7AE441. sub_7B7680 0x7B76F9 plays the hit sound (sub_79FD40) on a hit motion event only while set, motion event 9 clears it 0x7B776B [V]
                                              // Written by Btl_StartCommandExec 0x7AE370, sub_7B7680 0x7B7680. Read by sub_7B7680 0x7B7680
  /* 0xE1A */ __int16 m_hitSeOverride;   // AI property 260. Read signed by sub_7B7680 0x7B7702: when >= 0 it replaces the hit sound chosen from the hit result (0, 1, 2), 255 plays none. Reset to -1 by StartCommandExec 0x7AE41C and FinishActions 0x7914C4. Signed reads prove __int16 [V]
                                         // AI property 260. Written by Btl_FinishActions 0x7911E0, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B7680 0x7B7680
  /* 0xE1C */ unsigned __int16 m_overlayEffectSerial;   // serial for kind 2 effect handles. Effect state 11 in FFX_Btl_StepMagicEffectStates 0x7886F7 packs it into the handle passed to overlay slot 0, then stores +1 at 0x78870E [V]
                                                        // Written by sub_788480 0x788480. Read by sub_788480 0x788480
  /* 0xE1E */ unsigned __int16 m_sharedFxSerial;   // serial shared by several effect handle kinds through FFX_Magic_PackEffectHandle: 7 (sub_79ECA0), 8 (sub_79EC40), 12 (sub_79E590 0x79E5AA), 4 and 15 (sub_79E3D0 0x79E47D), and every kind except 1 in sub_79EB10 0x79EB51 [V]
                                                   // Written by sub_79EC40 0x79EC40, sub_79ECA0 0x79ECA0, sub_79E590 0x79E590 and 2 more. Read by sub_79E3D0 0x79E3D0, sub_79EC40 0x79EC40, sub_79ECA0 0x79ECA0 and 2 more
  /* 0xE20 */ unsigned __int16 m_statusFxSerial;   // serial for kind 3 effect handles, the status icon effects sub_79EF40 0x79EF7F starts into m_statusFx, incremented after use [V]
                                                   // Written by sub_79EF40 0x79EF40. Read by sub_79EF40 0x79EF40
  /* 0xE22 */ unsigned __int16 m_kind1FxSerial;   // serial for kind 1 effect handles, used by sub_79EB10 0x79EB3E when its kind is 1 (Btl 130 PlayEffectKind1) [V]
                                                  // Written by sub_79EB10 0x79EB10. Read by sub_79EB10 0x79EB10
  /* 0xE24 */ unsigned __int16 m_prepFxSerial;   // serial for kind 5 effect handles, the Btl 064 effect started by sub_79E8C0 0x79E9BE and stored in m_prepFxHandle [V]
                                                 // Written by sub_79E8C0 0x79E8C0. Read by sub_79E8C0 0x79E8C0
  /* 0xE26 */ unsigned __int16 m_reactionFxSerial;   // serial for kind 9 effect handles, the per target effect 15 sub_79F160 0x79F186 starts into m_reactionFxHandle [V]
                                                     // Written by sub_79F160 0x79F160. Read by sub_79F160 0x79F160
  /* 0xE28 */ unsigned __int16 m_statusFx2Serial;   // serial for kind 13 effect handles, the four effects 60..63 sub_79ED40 0x79EDBD starts into m_statusFx2 [V]
                                                    // Written by sub_79ED40 0x79ED40. Read by sub_79ED40 0x79ED40
  /* 0xE2A */ unsigned __int16 m_kind14FxSerial;   // serial for kind 14 effect handles in sub_79EBE0 0x79EBFA, which counts those effects in the global dword_112C8F8 [V]
                                                   // Written by sub_79EBE0 0x79EBE0. Read by sub_79EBE0 0x79EBE0
  /* 0xE2C */ int m_iE2C;   // AI property 87. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by maybe_Btl_SetupUnitChr 0x793670
  /* 0xE30 */ int m_statusFx[24];   // one effect handle per status icon or aura, started by sub_79EF40 0x79EF40 from the status words and
                                    // counters, stopped with sub_7FC450 by FFX_Btl_StopStatusFx 0x79F110
  /* 0xE90 */ int m_statusFx2[4];   // four more effect handles, set by sub_79ED40 0x79ED40, stopped by sub_79EA90 0x79EA90
  /* 0xEA0 */ int m_prepFxHandle;   // kind 5 effect handle sub_79E8C0 0x79E9FB stores when it plays m_prepFxGround or m_prepFxAirborne. sub_79E830 0x79E86D stops it and zeroes it, called by effect state 11 and FFX_Btl_FinishActions [V]
                                    // Written by sub_79E830 0x79E830, sub_79E8C0 0x79E8C0. Read by sub_79E830 0x79E830, sub_79E8C0 0x79E8C0
  /* 0xEA4 */ int m_reactionFxHandle;   // kind 9 handle of effect 15 that sub_79F160 0x79F1A8 starts on each target. FFX_Btl_StepEffectReactions 0x7890A6 waits until no target has it nonzero before it marks the hits done [V]
                                        // Written by sub_79F160 0x79F160. Read by sub_789030 0x789030, sub_79F160 0x79F160
  /* 0xEA8 */ int m_chrEffectHandle;   // handle FFX_Ch_AttachChrEffect returns, stored by Btl 131 0x7A4300. sub_788E30 0x788E37 releases it with FFX_Ch_ReleaseChrEffect and zeroes it, called by FFX_Btl_FinishActions [V]
                                       // Written by AtelSys_Btl_131_resi 0x7A42A0, sub_788E30 0x788E30. Read by sub_788E30 0x788E30, AtelSys_Btl_131_resi 0x7A42A0
  /* 0xEAC */ int m_soundHandles[32];   // handles FFX_Btl_QueueSoundSections 0x7A01B0 makes with sub_886410 from the unit's data file, released with sub_886D80
                                        // by sub_7A0400 0x7A0400. Btl 108 plays entry n of the m_primaryUnit's list through sub_7A0A30.
                                        // That these are sound banks is [I]
  /* 0xF2C */ unsigned __int8 m_soundReady;   // set by FFX_Btl_ServiceSoundQueue 0x7A0540 when m_soundLoadsLeft reaches 0, cleared by sub_7A0400. Btl 108 needs it
  /* 0xF2D */ unsigned __int8 m_soundLoadsLeft;   // loads still in flight, counted down by FFX_Btl_ServiceSoundQueue
  /* 0xF2E */ unsigned __int8 m_soundLoadsIssued;   // loads issued by FFX_Btl_QueueSoundSections, polled by Btl 124
  unsigned __int8 _unk_F2F[0x5];
  /* 0xF34 */ int m_screenRawX;   // raw x from sub_6428F0 for the unit's bone at +0xDF9 (or its centre), written by sub_79F380 0x79F44A each pass. Returned by sub_795920 0x79594A [V]
                                  // Written by sub_79F380 0x79F380. Read by sub_795920 0x795920
  /* 0xF38 */ int m_screenRawY;   // raw y from sub_6428F0 for the same point, written by sub_79F380 0x79F459, returned by sub_795920 [V]
                                  // Written by sub_79F380 0x79F380. Read by sub_795920 0x795920
  /* 0xF3C */ int m_screenX;   // that point in the 512 wide virtual screen, -512 when behind the camera, sub_79F380 0x79F4B2. sub_79F660 0x79F7E0 draws the Doom counter digits from it [V]
                               // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF40 */ int m_screenY;   // that point in the 416 high virtual screen, sub_79F380 0x79F4BF. sub_79F660 0x79F7E6 draws the Doom counter 19 above it [V]
                               // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF44 */ int m_overheadScreenX;   // virtual x of the point above the unit (bone +0x442 or sub_794AB0, y less +0x4E8), sub_79F380 0x79F573. sub_79F660 0x79F8E9 anchors popup list entries with a non negative first byte to it [V]
                                       // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF48 */ int m_overheadScreenY;   // virtual y of the same point, sub_79F380 0x79F579, read by sub_79F660 0x79F8EF [V]
                                       // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF4C */ int m_overheadScreenClampX;   // m_overheadScreenX clamped to 27..485 by sub_79F380 0x79F5B0. sub_79F660 0x79F8CD uses it for popup entries with a negative first byte [V]
                                            // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF50 */ int m_overheadScreenClampY;   // m_overheadScreenY clamped to 34..391 by sub_79F380 0x79F5B6, read by sub_79F660 0x79F8D3 [V]
                                            // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF54 */ int m_menuAnchorX;   // virtual screen x FFX_BtlUi_ProjectUnitToScreen 0x795519 computes for the battle menu UI, -512 when off screen. Returned by sub_794FF0 0x79501A [V]
                                   // Written by sub_795250 0x795250. Read by sub_794FF0 0x794FF0
  /* 0xF58 */ int m_menuAnchorY;   // virtual screen y FFX_BtlUi_ProjectUnitToScreen 0x795522 computes, clamped against the window. Returned by sub_794FF0 [V]
                                   // Written by sub_795250 0x795250. Read by sub_794FF0 0x794FF0
  /* 0xF5C */ unsigned __int16 m_execCommandId;   // command id of the action being executed, word 0 of the action entry.
                                               // FFX_Btl_StartCommandExec 0x7AE433 and FFX_Btl_ApplyActionToTargets 0x789340 write it.
                                               // The overdrive code reads it back (FFX_BtlOd_StartMinigameForActor 0x7AFD4D,
                                               // FFX_BtlOd_WakkaReels_GetLevel 0x7B1A0E subtracts 0x3074). Was m_odMinigameId [V 2026-10-28]
  /* 0xF5E */ unsigned __int8 m_hitFlags;   // the action flags & 0x7F of the last applied hit (FFX_Btl_ApplyPendingEffects 0x78F0CD)
  /* 0xF5F */ unsigned __int8 m_overkilled;   // set 1 by FFX_Btl_OnUnitDefeated 0x78C87E when m_overkillHp <= 0 (and debug OverKillOff is off), with the overkill floating number. Passed to FFX_Btl_PayKillRewards 0x78C942. Cleared by FFX_Btl_ResetUnitForReentry 0x78D5E7 [V]
                                              // Written by Btl_OnUnitDefeated 0x78C740, Btl_ResetUnitForReentry 0x78D530. Read by Btl_OnUnitDefeated 0x78C740
  /* 0xF60 */ int m_overkillHp;   // overkill accumulator, seeded from m_overkillThreshold and reduced by every HP hit
  /* 0xF64 */ unsigned __int8 m_equipSwapSide;   // 0 weapon or 1 armour, set by sub_7AD940 0x7AD952 for commands 0x3004/0x3005 when the effect state is idle. sub_7AD820 0x788873 swaps that side's model, Btl 180 0x7A7168 fades that side record [V]
                                                 // Written by sub_7AD940 0x7AD940. Read by sub_788480 0x788480, AtelSys_Btl_180_resi 0x7A7130
  /* 0xF65 */ unsigned __int8 m_equipSwapFadeFrames;   // Btl 180 0x7A7155 sets 8 and fades the side CHR out over it (FFX_Ch_SetShadeAlpha mode). Effect state 9 in FFX_Btl_StepMagicEffectStates 0x78884E counts it down, then swaps the equipment [V]
                                                       // Written by sub_788480 0x788480, AtelSys_Btl_180_resi 0x7A7130. Read by sub_788480 0x788480, AtelSys_Btl_180_resi 0x7A7130
  /* 0xF66 */ unsigned __int16 m_equipSwapItem;   // equipment index (low 12 bits of the command argument) sub_7AD940 0x7AD967 stores. sub_7AD820 0x78886B writes it to the save with FFX_SaveData__setCharEquip and reloads the model if it differs [V]
                                                  // Written by sub_7AD940 0x7AD940. Read by sub_788480 0x788480
  /* 0xF68 */ float m_lineSortDistance;   // scratch key of FFX_Btl_SortTargetOrderAcrossLine 0x7B738A: each target's signed distance from the line between selectors -14 and -15 (sub_7B6F10), compared at 0x7B73D3 to order the list [V]
                                          // Written by sub_7B72C0 0x7B72C0. Read by sub_7B72C0 0x7B72C0
  /* 0xF6C */ float m_doomPulsePhase;   // phase sub_79F660 0x79F70F advances by (+0x5C9 / (+0x5C8 + 1)) * 0.125 each pass while Doom (+0x616 bit 0x4000) is up. Its sine pulses the colour of the Doom counter digits [V]
                                        // Written by sub_79F660 0x79F660. Read by sub_79F660 0x79F660
  /* 0xF70 */ float m_koPosY;   // unit Y position sub_78DDC0 0x78DDEC records when it sets m_ko to 2 and fires event 63. sub_794AB0 0x794B2E uses it as the Y when m_ko is 2 and BYTE2(dword_112C9DB) is set [V]
                                // Written by sub_78DDC0 0x78DDC0. Read by sub_794AB0 0x794AB0
  /* 0xF74 */ unsigned __int8 m_descTextOverride;   // AI property 235, 0xFF at roster setup 0x79C282. When not 0xFF, sub_78F7C0 0x78F82E returns battle message entry n of dword_112A9B8 instead of the unit's m_description text (kind 1, used by the scan panel sub_899FD0) [V]
                                                    // AI property 235. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_78F7C0 0x78F7C0
  /* 0xF75 */ unsigned __int8 m_scanTextOverride;   // AI property 236, 0xFF at roster setup. When not 0xFF, sub_78F7C0 0x78F825 returns battle message entry n of dword_112A9B8 instead of the unit's m_scanText (kind 2) [V]
                                                    // AI property 236. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_78F7C0 0x78F7C0
  /* 0xF76 */ unsigned __int16 m_prepFxSe;   // sound id sub_79E8C0 0x79E9D4 plays with sub_7A0100 alongside the Btl 064 effect (25, 26 or 62), also copied to the aeon out unit. Replayed with sub_7A00D0 by FFX_Btl_StartEffectReactions 0x789014 and FFX_Btl_PlaceArrivedAeon [V] for the id, the 20 argument [I]
                                             // Written by sub_79E8C0 0x79E8C0. Read by Btl_PlaceArrivedAeon 0x7AC6A0, sub_788F90 0x788F90
  /* 0xF78 */ void *m_atelBlock;   // monster ATEL script block, passed to FFX_BtlEv_AddScriptBlock by Btl_RegisterMonsterMotions 0x7840BB
  /* 0xF7C */ void *m_eventMap;   // event lookup table for FFX_BtlEv_LookupEventEntry (BtlEv_StartFirstMatch 0x797E2F)
  /* 0xF80 */ void *m_params;   // monster archive section 3, the stat and parameter record (FFX_Btl_BindMonsterArchiveSections 0x783B32)
  /* 0xF84 */ void *m_dataSec7;   // monster archive section at header dword [7] (FFX_Btl_BindMonsterArchiveSections 0x783B3D). No reader found
  /* 0xF88 */ void *m_rewards;   // the 280 byte reward record at g_ffxBtlRewardRecords. +6 is the Ronso Rage the unit teaches
  /* 0xF8C */ int m_effectOverlayTable;   // the magic DLL's 16 slot effect overlay table from MagicFile__getEffectOverlayTable, stored by FFX_Btl_StartMagicPrx 0x788163. Nonzero placeholder while loading. Slot 0 called at state 11 0x7886F1, slot 1 by FFX_Btl_FreeActorMagicEffect 0x787BA0 [V]
                                          // Written by Btl_StartMagicPrx 0x788110, Btl_BeginMagicEffect 0x787F20, sub_7AD820 0x7AD820 and 2 more. Read by sub_788480 0x788480, sub_787AF0 0x787AF0, Magic_CallOverlayStep 0x787E00 and 2 more
};

// SAVE_EQUIP_ENTRY   size=0x16 (22)
// One piece of equipment. 200 of them at SAVEDATA.m_equipment. See CHEAT_SAVEDATA.md section 4 and
// SAVE_BLOCK_MAP.md.
struct SAVE_EQUIP_ENTRY {
  /* 0x00 */ unsigned __int16 m_nameId;   // 0x5000 based, from FFX_Equip_ComputeNameId
  /* 0x02 */ unsigned __int8 m_inUse;   // 0 = free slot
  /* 0x03 */ unsigned __int8 m_flags;   // 0x02 no customise, 0x04 celestial weapon, 0x08 celestial armour
  /* 0x04 */ unsigned __int8 m_forChar;   // 0..6, picks the w_name column and the model
  /* 0x05 */ unsigned __int8 m_kind;   // 0 weapon, else armour
  /* 0x06 */ unsigned __int8 m_owner;   // record index equipping it, 0xFF none
  /* 0x07 */ unsigned __int8 _unk_7;   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x08 */ unsigned __int8 m_actor5c1;   // FFX_Btl_LoadCharRecordIntoActor 0x79C5F0 -> actor +0x5C1
  /* 0x09 */ unsigned __int8 m_actor5c7;   // FFX_Btl_LoadCharRecordIntoActor -> actor +0x5C7
  /* 0x0A */ unsigned __int8 m_actor5d8Add;   // FFX_Btl_LoadCharRecordIntoActor adds both entries' value into actor +0x5D8
  /* 0x0B */ unsigned __int8 m_abilitySlotCount;   // 1..4
  /* 0x0C */ unsigned __int16 m_modelId;   // 0x40xx
  /* 0x0E */ unsigned __int16 m_abilities[4];   // 0 and 0xFF are empty
};

// SAVE_CHAR_RECORD   size=0x94 (148)
// One character, 18 of them at SAVEDATA.m_charRecords: 0..6 the party, 7 Seymour, 8..17 the aeons.
// FFX_InitNewSaveData copies a ply_save.bin row over each. See CHEAT_SAVEDATA.md section 3.
struct SAVE_CHAR_RECORD {
  /* 0x00 */ unsigned __int16 m_templateNameOffset;   // carried from ply_save.bin, never read
  /* 0x02 */ unsigned __int16 m_templateWord02;   // carried from ply_save.bin, never read
  /* 0x04 */ int m_baseMaxHp;
  /* 0x08 */ int m_baseMaxMp;
  /* 0x0C */ unsigned __int8 m_baseStrength;
  /* 0x0D */ unsigned __int8 m_baseDefense;
  /* 0x0E */ unsigned __int8 m_baseMagic;
  /* 0x0F */ unsigned __int8 m_baseMagicDefense;
  /* 0x10 */ unsigned __int8 m_baseAgility;
  /* 0x11 */ unsigned __int8 m_baseLuck;
  /* 0x12 */ unsigned __int8 m_baseEvasion;
  /* 0x13 */ unsigned __int8 m_baseAccuracy;
  /* 0x14 */ int m_apTotal;
  /* 0x18 */ int m_apBanked;
  /* 0x1C */ int m_hp;   // re-clamped on every recompute
  /* 0x20 */ int m_mp;   // re-clamped on every recompute
  /* 0x24 */ int m_maxHp;   // FFX_SaveData_RecomputeCharDerived. DERIVED
  /* 0x28 */ int m_maxMp;   // FFX_SaveData_RecomputeCharDerived. DERIVED
  /* 0x2C */ unsigned __int8 m_partyFlags;   // bit 0 in the party
  /* 0x2D */ unsigned __int8 m_weaponSlot;   // equipment index, 0xFF none
  /* 0x2E */ unsigned __int8 m_armourSlot;   // equipment index, 0xFF none
  /* 0x2F */ unsigned __int8 m_effectiveStats[8];   // str def mag mdef agi luck eva acc, recomputed. DERIVED
  /* 0x37 */ unsigned __int8 m_poisonPercent;   // FFX_Btl_LoadUnitParams 0x79B4F0 -> actor +0x5BA
  /* 0x38 */ unsigned __int8 m_overdriveBytes[3];   // copied back from actor +1467..1469 after a battle
  /* 0x3B */ unsigned __int8 m_sphereLevels;
  /* 0x3C */ unsigned __int8 m_sphereLevelsSpent;
  /* 0x3D */ unsigned __int8 m_reviveCountdown;
  /* 0x3E */ unsigned __int16 m_abilityFlags[6];   // ids 0x3000..0x305F, the grid ORs into these
  /* 0x4A */ unsigned __int16 m_autoAbilityFlags[3];   // cleared and rebuilt from the equipped gear on every recompute. DERIVED
  /* 0x50 */ int m_battleCounters[4];   // FFX_SaveData_BumpCharBattleCounter
  /* 0x60 */ unsigned __int16 m_overdriveModeCounters[20];   // per mode, 0xFFFF n/a. FFX_Btl_BumpOverdriveModeCounter 0x7B10C0
  /* 0x88 */ unsigned int m_overdriveModeMask;   // learned modes, FFX_Menu_GetCharOverdriveModeMask 0x8C1BF0
  /* 0x8C */ unsigned __int8 _unk_8c[8];   // unused, SAVE_BLOCK_MAP.md section 4
};

// SAVE_CHAR_NAME   size=0x14 (20)
// A character's display name, 18 of them at SAVEDATA.m_charNames.
struct SAVE_CHAR_NAME {
  /* 0x00 */ unsigned __int8 m_text[18];   // FFX glyph encoded
  /* 0x12 */ unsigned __int8 m_language;   // FFX_GetLanguage at the time of the rename, per machine
  /* 0x13 */ unsigned __int8 m_renamed;   // 1 once the player named the character
};

// SAVEDATA   size=0x68C0 (26816)
// g_ffxSaveData 0x112CA90, the whole save block. The file is a 0x40 byte header plus a byte for byte
// copy of this. Every byte is classified in SAVE_BLOCK_MAP.md, and tools/saveblock_fields.py is the
// same layout as data (this struct is generated from it). It holds no pointers. The script_vars runs
// are ATEL class 0 variables (offset = class 0 offset + 0x1EC), owned by the scripts, not the C side.
struct SAVEDATA {
  /* 0x0000 */ unsigned __int16 m_mapId;   // live map id, FFX_SaveData_GetMapId 0x88D660
  /* 0x0002 */ unsigned __int16 m_prevMapId;   // FFX_SaveData_GetPrevMapId 0x88D600
  /* 0x0004 */ unsigned __int16 m_sceneId;   // FFX_SaveData_GetSceneId 0x88D690
  /* 0x0006 */ unsigned __int16 m_prevSceneId;
  /* 0x0008 */ unsigned __int16 m_sceneSubId;
  /* 0x000A */ unsigned __int16 m_prevSceneSubId;
  /* 0x000C */ unsigned __int8 m_entryPoint;   // live entry point, an index into the event package's warp table
  /* 0x000D */ unsigned __int8 m_prevEntryPoint;
  /* 0x000E */ unsigned __int16 m_mapNameId;   // set from the ground dictionary on map entry, SetMapNameId / SetMapNameIdFromGroundDic. DERIVED
  /* 0x0010 */ unsigned __int8 m_encounterGroundAttr;   // rewritten every field step from the controlled actor's ground triangle. DERIVED
  /* 0x0011 */ unsigned __int8 m_warpFadeKind;   // core:188
  /* 0x0012 */ unsigned __int8 m_fadeOutFrames;   // core:187
  /* 0x0013 */ unsigned __int8 m_fieldFlags;   // bit 1 encounters allowed, StepFrame rewrites it every step. DERIVED
  /* 0x0014 */ unsigned __int8 _unk_14[4];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0018 */ unsigned __int64 m_headerBits;   // 64 bits, FFX_SaveData_TestHeaderBit18 0x86B680 / SetHeaderBit18 0x8700E0
  /* 0x0020 */ unsigned __int8 m_partySnapshotValid;   // FFX_Atel_PushPartySnapshot 0x86E310 / PopPartySnapshot 0x86DDB0
  /* 0x0021 */ unsigned __int8 m_partySnapshot[3];
  /* 0x0024 */ unsigned int m_partySnapshotMask;
  /* 0x0028 */ unsigned __int8 m_indoor;   // FFX_Field_UpdateIndoorFlag 0x871E50 compares camera height with ctx+520, then SetIndoor copies it here. DERIVED
  /* 0x0029 */ unsigned __int8 m_mapOutFlag;   // bit 0, FFX_Field_UpdateIndoorFlag, per map. DERIVED
  /* 0x002A */ unsigned __int8 m_loadedFromFile;   // 1 in every file image (Serialize marks the copy), consumed once by SetSceneAndSub. SCRATCH
  /* 0x002B */ unsigned __int8 m_scriptByte2b;   // core:250 writes it in 9 places, only core:249 (dead) and a debug page read it. SCRATCH
  /* 0x002C */ unsigned int m_loveParam[8];   // core:301..304
  /* 0x004C */ unsigned __int8 m_eventFlagBits[80];   // 640 bits, core:305, 568, 569 and FFX_Atel_GrantTalkBonusOnce
  /* 0x009C */ float m_savePlayerPos[3];   // written only into the staging image at save time, read back from the staging image on load. The live copy is never read
  /* 0x00A8 */ unsigned __int16 m_checkpointBgm;   // 0xFFFF in the staging image during a manual save
  /* 0x00AA */ unsigned __int16 m_checkpointMovieWord;   // word_C322A8 + 1, the background movie id plus a flag [I]. 0 in manual saves
  /* 0x00AC */ unsigned __int8 _unk_ac[8];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x00B4 */ unsigned __int16 m_worldMapMarkerX;   // core:366, drawn by the save screen as the scene30 sprite at x/2
  /* 0x00B6 */ unsigned __int16 m_worldMapMarkerY;
  /* 0x00B8 */ unsigned __int16 m_checkpointEntry;   // maybe_FFX_Scene_Init and core:367
  /* 0x00BA */ unsigned __int16 m_checkpointMap;   // maybe_FFX_Scene_Init and core:367
  /* 0x00BC */ unsigned int m_playtime;   // seconds, clamped to 3,599,999. WALL CLOCK, see SAVE_BLOCK_MAP.md
  /* 0x00C0 */ unsigned __int16 m_locationHistory[8];   // FFX_SaveData_SnapshotLocationHistory 0x88E800
  /* 0x00D0 */ unsigned __int8 m_battleReturnMode;   // written by FFX_Btl_ReturnToField and Reset, no reader. SCRATCH
  /* 0x00D1 */ unsigned __int8 m_rikkuAltOutfit;   // core:404, FFX_SaveData_GetRikkuAltOutfit
  /* 0x00D2 */ unsigned __int8 m_shadowFlag;   // only Reset writes it, FFX_Ch_ShadowsEnabled reads it
  /* 0x00D3 */ unsigned __int8 _unk_d3;   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x00D4 */ unsigned __int16 m_mapNameOverride;   // FFX_SaveData_SetMapNameOverride
  /* 0x00D6 */ unsigned __int8 m_mapNameOverrideActive;
  /* 0x00D7 */ unsigned __int8 m_scriptFlagD7;   // core:445, read by sub_7829B0
  /* 0x00D8 */ unsigned int m_premovieNumber;   // premovie bookkeeping, cleared on map unload. SCRATCH
  /* 0x00DC */ unsigned int m_premovieMode;   // SCRATCH
  /* 0x00E0 */ unsigned int m_premovieState;   // SCRATCH
  /* 0x00E4 */ unsigned int m_fieldBootFlags;   // cleared by maybe_FFX_Map_Unload and sub_872BB0. SCRATCH
  /* 0x00E8 */ unsigned __int16 m_fieldWordE8;   // cleared by maybe_FFX_Map_Unload and sub_872BB0. SCRATCH
  /* 0x00EA */ unsigned __int16 m_scriptWordEa;   // core:525, FFX_SaMenu_GetSaveWordEA
  /* 0x00EC */ char m_nameEntryText[32];   // free text from the name entry screen, compared by core:466
  /* 0x010C */ unsigned __int8 _unk_10c[224];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x01EC */ unsigned __int8 m_scriptVars01ec[60];   // declared by shipped scripts
  /* 0x0228 */ unsigned __int8 _unk_228[68];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x026C */ unsigned __int8 m_scriptVars026c[29];   // declared by shipped scripts
  /* 0x0289 */ unsigned __int8 _unk_289[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x028B */ unsigned __int8 m_scriptVars028b[7];   // declared by shipped scripts
  /* 0x0292 */ unsigned __int8 _unk_292[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0294 */ unsigned __int8 m_scriptVars0294[19];   // declared by shipped scripts
  /* 0x02A7 */ unsigned __int8 _unk_2a7[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x02A8 */ unsigned __int8 m_scriptVars02a8[31];   // declared by shipped scripts
  /* 0x02C7 */ unsigned __int8 _unk_2c7[37];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x02EC */ unsigned __int8 m_scriptVars02ec[2];   // declared by shipped scripts
  /* 0x02EE */ unsigned __int8 _unk_2ee[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x02F0 */ unsigned __int8 m_scriptVars02f0[2];   // declared by shipped scripts
  /* 0x02F2 */ unsigned __int8 _unk_2f2[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x02F4 */ unsigned __int8 m_scriptVars02f4[3];   // declared by shipped scripts
  /* 0x02F7 */ unsigned __int8 _unk_2f7[3];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x02FA */ unsigned __int8 m_scriptVars02fa[3];   // declared by shipped scripts
  /* 0x02FD */ unsigned __int8 _unk_2fd[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x02FE */ unsigned __int8 m_scriptVars02fe[1];   // declared by shipped scripts
  /* 0x02FF */ unsigned __int8 _unk_2ff[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0300 */ unsigned __int8 m_scriptVars0300[4];   // declared by shipped scripts
  /* 0x0304 */ unsigned __int8 _unk_304[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0305 */ unsigned __int8 m_scriptVars0305[25];   // declared by shipped scripts
  /* 0x031E */ unsigned __int8 _unk_31e[14];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x032C */ unsigned __int8 m_scriptVars032c[12];   // declared by shipped scripts
  /* 0x0338 */ unsigned __int8 _unk_338[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x033A */ unsigned __int8 m_scriptVars033a[2];   // declared by shipped scripts
  /* 0x033C */ unsigned __int8 _unk_33c[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x033D */ unsigned __int8 m_scriptVars033d[27];   // declared by shipped scripts
  /* 0x0358 */ unsigned __int8 _unk_358[20];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x036C */ unsigned __int8 m_scriptVars036c[23];   // declared by shipped scripts
  /* 0x0383 */ unsigned __int8 _unk_383[41];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x03AC */ unsigned __int8 m_scriptVars03ac[26];   // declared by shipped scripts
  /* 0x03C6 */ unsigned __int8 _unk_3c6[38];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x03EC */ unsigned __int8 m_scriptVars03ec[14];   // declared by shipped scripts
  /* 0x03FA */ unsigned __int8 _unk_3fa[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x03FC */ unsigned __int8 m_scriptVars03fc[7];   // declared by shipped scripts
  /* 0x0403 */ unsigned __int8 _unk_403[41];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x042C */ unsigned __int8 m_scriptVars042c[24];   // declared by shipped scripts
  /* 0x0444 */ unsigned __int8 _unk_444[9];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x044D */ unsigned __int8 m_scriptVars044d[7];   // declared by shipped scripts
  /* 0x0454 */ unsigned __int8 _unk_454[24];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x046C */ unsigned __int8 m_scriptVars046c[6];   // declared by shipped scripts
  /* 0x0472 */ unsigned __int8 _unk_472[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0474 */ unsigned __int8 m_scriptVars0474[8];   // declared by shipped scripts
  /* 0x047C */ unsigned __int8 _unk_47c[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x047D */ unsigned __int8 m_scriptVars047d[10];   // declared by shipped scripts
  /* 0x0487 */ unsigned __int8 _unk_487[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0488 */ unsigned __int8 m_scriptVars0488[29];   // declared by shipped scripts
  /* 0x04A5 */ unsigned __int8 _unk_4a5[327];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x05EC */ unsigned __int16 m_menuConfig5ec;   // bit 0, FFX_Menu_GetSaveConfigBit5EC. In a gap no script declares
  /* 0x05EE */ unsigned __int8 _unk_5ee[318];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x072C */ unsigned __int8 m_scriptVars072c[700];   // declared by shipped scripts
  /* 0x09E8 */ unsigned __int8 _unk_9e8[4];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x09EC */ unsigned __int8 m_scriptVars09ec[2];   // declared by shipped scripts
  /* 0x09EE */ unsigned __int8 _unk_9ee[54];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0A24 */ unsigned __int8 m_scriptVars0a24[80];   // declared by shipped scripts
  /* 0x0A74 */ unsigned __int8 _unk_a74[8];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0A7C */ unsigned __int8 m_scriptVars0a7c[8];   // declared by shipped scripts
  /* 0x0A84 */ unsigned __int8 _unk_a84[26];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0A9E */ unsigned __int8 m_scriptVars0a9e[3];   // declared by shipped scripts
  /* 0x0AA1 */ unsigned __int8 _unk_aa1[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0AA2 */ unsigned __int8 m_scriptVars0aa2[12];   // declared by shipped scripts
  /* 0x0AAE */ unsigned __int8 _unk_aae[318];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0BEC */ unsigned __int8 m_scriptVars0bec[2];   // declared by shipped scripts
  /* 0x0BEE */ unsigned __int8 _unk_bee[6];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0BF4 */ unsigned __int8 m_scriptVars0bf4[4];   // declared by shipped scripts
  /* 0x0BF8 */ unsigned __int8 _unk_bf8[4];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0BFC */ unsigned __int8 m_scriptVars0bfc[2];   // declared by shipped scripts
  /* 0x0BFE */ unsigned __int8 _unk_bfe[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C00 */ unsigned __int8 m_scriptVars0c00[12];   // declared by shipped scripts
  /* 0x0C0C */ unsigned __int8 _unk_c0c[16];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C1C */ unsigned __int8 m_scriptVars0c1c[9];   // declared by shipped scripts
  /* 0x0C25 */ unsigned __int8 _unk_c25[3];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C28 */ unsigned __int8 m_scriptVars0c28[2];   // declared by shipped scripts
  /* 0x0C2A */ unsigned __int8 _unk_c2a[6];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C30 */ unsigned __int8 m_scriptVars0c30[4];   // declared by shipped scripts
  /* 0x0C34 */ unsigned __int8 _unk_c34[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C35 */ unsigned __int8 m_scriptVars0c35[2];   // declared by shipped scripts
  /* 0x0C37 */ unsigned __int8 _unk_c37[1];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C38 */ unsigned __int8 m_scriptVars0c38[2];   // declared by shipped scripts
  /* 0x0C3A */ unsigned __int8 _unk_c3a[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C3C */ unsigned __int8 m_scriptVars0c3c[17];   // declared by shipped scripts
  /* 0x0C4D */ unsigned __int8 _unk_c4d[4];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C51 */ unsigned __int8 m_scriptVars0c51[12];   // declared by shipped scripts
  /* 0x0C5D */ unsigned __int8 _unk_c5d[3];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C60 */ unsigned __int8 m_scriptVars0c60[6];   // declared by shipped scripts
  /* 0x0C66 */ unsigned __int8 _unk_c66[2];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0C68 */ unsigned __int8 m_scriptVars0c68[42];   // declared by shipped scripts
  /* 0x0C92 */ unsigned __int8 _unk_c92[69];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x0CD7 */ unsigned __int8 m_scriptVars0cd7[143];   // declared by shipped scripts
  /* 0x0D66 */ unsigned __int8 _unk_d66[1158];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x11EC */ unsigned __int8 m_scriptVars11ec[1098];   // declared by shipped scripts
  /* 0x1636 */ unsigned __int8 _unk_1636[8];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x163E */ unsigned __int8 m_scriptVars163e[1454];   // declared by shipped scripts
  /* 0x1BEC */ unsigned __int8 _unk_1bec[1536];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x21EC */ unsigned __int8 m_gridNodes[2560];   // 1280 x {panel kind, activation mask}
  /* 0x2BEC */ unsigned __int8 m_gridLinks[1280];   // one byte per grid line, a per-character mask
  /* 0x30EC */ unsigned __int16 m_gridCurrentNode[7];   // per character 0..6
  /* 0x30FA */ unsigned __int8 _unk_30fa[10];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x3104 */ unsigned __int8 m_gridId;   // 0..2
  /* 0x3105 */ unsigned __int8 m_gridZoom;   // 0..3
  /* 0x3106 */ unsigned __int8 _unk_3106[1030];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x350C */ unsigned __int8 m_guideMap582[512];   // 64 x 64 explored cell bitmap, set each step around the local player
  /* 0x370C */ unsigned __int8 m_guideMap584[512];
  /* 0x390C */ unsigned __int8 m_guideMap590[512];
  /* 0x3B0C */ unsigned __int8 m_guideMap591[512];
  /* 0x3D0C */ unsigned int m_optionBits;   // settings, also the sphere grid type in bits 14..15 and the text variant in bit 16
  /* 0x3D10 */ unsigned int m_alBhedPrimerMask;   // 26 bits
  /* 0x3D14 */ unsigned int m_battleCount;   // FFX_Field_StepRandomEncounter and FFX_Btl_StepSubPhase add 1
  /* 0x3D18 */ unsigned __int8 _unk_3d18[16];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x3D28 */ unsigned __int16 m_innateAbilityBits[16];   // kernel table 31 copy, words 0..5 ids 0x3000..0x305F, words 6..15 ids 0x3060 and up
  /* 0x3D48 */ unsigned int m_gil;   // FFX_SaveData_GetGil
  /* 0x3D4C */ unsigned int m_rngSeedBase;   // kernel conf value, read once by FFX_InitNewSaveData to seed the RNG streams
  /* 0x3D50 */ unsigned __int8 _unk_3d50[8];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x3D58 */ unsigned __int8 m_party[3];   // the three active members, record indexes
  /* 0x3D5B */ unsigned __int8 m_aeonOrder[17];   // FFX_Btl_CommitAeonOrderToSave
  /* 0x3D6C */ unsigned __int16 m_sharedAbilityBits[16];   // ids 0x3060 and up, bytes 28..31 are not addressable by a valid id
  /* 0x3D8C */ unsigned __int16 m_startItemIds[8];   // conf template, read once by FFX_InitNewSaveData
  /* 0x3D9C */ unsigned __int8 m_startItemCounts[8];   // conf template, read once by FFX_InitNewSaveData
  /* 0x3DA4 */ unsigned int m_yojimboMotivation;   // 0..255, sub_7B2950 / sub_7B2B20
  /* 0x3DA8 */ unsigned int m_yojimboWord;   // Btl 233 and 234, sub_7B2B30 / sub_7B2B40
  /* 0x3DAC */ unsigned int m_tidusOverdriveLearnCount;   // sub_7B0CD0 from the command cost path
  /* 0x3DB0 */ unsigned int m_rikkuStealCount;   // sub_78B760, trophy 4 at 200
  /* 0x3DB4 */ unsigned int m_gilSpent302a;   // gil paid through command 0x302A, trophy 19 at 100,000
  /* 0x3DB8 */ unsigned __int8 _unk_3db8[20];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x3DCC */ unsigned __int8 m_worldFlags[128];   // 1024 bits, FFX_SaveData_TestWorldFlag3DCC
  /* 0x3E4C */ unsigned __int8 _unk_3e4c[128];   // unused, SAVE_BLOCK_MAP.md section 4
  /* 0x3ECC */ unsigned __int16 m_itemIds[256];   // WORD[256], 0x2000 based
  /* 0x40CC */ unsigned __int8 m_itemCounts[256];   // BYTE[256]
  /* 0x41CC */ unsigned __int16 m_itemGainedMask[16];   // set only, by FFX_SaveData_MarkItemDelta from AddItem. Nothing reads it
  /* 0x41EC */ unsigned __int16 m_itemLostMask[16];   // set only, by FFX_SaveData_MarkItemDelta from AddItem. Nothing reads it
  /* 0x420C */ unsigned __int8 m_captureCounts[512];   // monster species 0..138 are valid indexes
  /* 0x440C */ unsigned __int8 m_monsterSeenMask[64];   // 512 bits
  /* 0x444C */ unsigned __int8 m_monsterDefeatedMask[64];   // 512 bits
  /* 0x448C */ unsigned __int8 m_keyItemBits[16];   // 128 bits, ids below 0x80, 64 real
  /* 0x449C */ SAVE_EQUIP_ENTRY m_equipment[200];   // 200 x 22
  /* 0x55CC */ SAVE_CHAR_RECORD m_charRecords[18];   // 18 x 0x94
  /* 0x6034 */ unsigned __int8 m_charAbilityBits[792];   // 18 x 44, bytes 40..43 of each row are not addressable by a valid command id
  /* 0x634C */ SAVE_CHAR_NAME m_charNames[18];   // 18 x 20
  /* 0x64B4 */ unsigned int m_crc;   // only written into the file image, 0 in a block loaded through the PC Load Game. SCRATCH
  /* 0x64B8 */ unsigned __int8 _unk_64b8[1032];   // unused, SAVE_BLOCK_MAP.md section 4
};
