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
  // 0x804..0x81F tune FFX_Ch_StepSecondaryPhysics 0x822DE0, the joint sway (hair, cloth) that runs on worker 0 for
  // CHRs with m_flags1 0x200000 and 0x400000
  /* 0x804 */ float m_windYaw;   // degrees. Used instead of the global wind (flt_12FBC48) while m_flags2 bit 0x2 is set
  /* 0x808 */ float m_windPitch;   // degrees, instead of flt_12FBC4C. sub_8450D0 turns the pair into a unit vector
  /* 0x80C */ float m_windStrength;   // times CHRDATA.m_f3C, instead of flt_12FBC44. All three set by sub_824C90 0x824C90 (ATEL Btl_282
                                     // 0x7A41D0, magic 0x816014), which also sets m_flags2 bit 0x2 through sub_824CC0
  /* 0x810 */ __int16 m_swaySettleAlways;   // 1 from sub_824920 0x824920 (FFX_Ch_Allocate). Non-zero runs m_swaySettleIters solver
                                           // passes every step instead of only after a large move or a snap
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
  /* 0x01A */ unsigned __int8 m_b1A;   // Written by Btl_SwapUnitIntoPartySlot 0x7ADAE0. No reader found
  /* 0x01B */ unsigned __int8 m_b1B;   // 1 at roster setup. sub_7821F0 0x78236F forces the end of battle visibility value to -1 when it is 0. AI property 217
  /* 0x01C */ unsigned __int8 m_b1C;   // sub_7821F0 0x782365 drops the end of battle visibility value to 0 when it is set. AI property 336
  /* 0x01D */ unsigned __int8 m_showWeapons;   // the weapon records follow m_visible only while this is set (FFX_Btl_SetUnitShowVote). FFX_Btl_ResolveUnitChrId
                                               // clears it for Rikku's alternate outfit
  /* 0x01E */ unsigned __int8 m_b1E;   // 1 for aeons (FFX_Btl_SetupUnitRoster) and from FFX_Btl_QueueAeonTransition. Read by maybe_FFX_Btl_SetupUnitChr 0x7938A5
  /* 0x01F */ unsigned __int8 m_b1F;   // Written by FFX_Btl_SetUnitShowVote 0x797080, sub_788E60 0x788E60, maybe_Btl_SetupUnitChr 0x793670. No reader found
  /* 0x020 */ unsigned __int8 m_motionSet;   // motion set index: FFX_Ch_SetLocomotionMode and FFX_Ch_SetB185 (sub_796F70 0x796F70), FFX_Mot_SetByModeIndex
                                             // (Btl 047), FFX_Ch_MotionSetReadSync. 1 normally, 3 for units 0..17 in an underwater battle
  /* 0x021 */ unsigned __int8 m_b21;   // 1 at setup. sub_793480 0x7934C8 passes it as bit 4 to sub_796DC0 at battle end. AI property 238
  /* 0x022 */ unsigned __int8 m_b22;   // sub_793480 0x7934BE passes it as bit 2 to sub_796DC0 at battle end. AI property 239
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
  /* 0x039 */ unsigned __int8 m_b39;   // AI property 339. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7AB360 0x7AB360
  /* 0x03A */ unsigned __int8 m_b3A;   // Written by sub_793D90 0x793D90, sub_7AD750 0x7AD750, sub_796DC0 0x796DC0. Read by sub_793D90 0x793D90
  /* 0x03B */ unsigned __int8 m_b3B;   // AI property 334. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_78DEB0 0x78DEB0
  /* 0x03C */ unsigned __int8 m_b3C;   // sub_7969C0 0x7969C0 passes (m_b3C || m_b3D) to sub_835780 on the CHR
  /* 0x03D */ unsigned __int8 m_b3D;   // see m_b3C. Set by FFX_Btl_SwapUnitIntoPartySlot, read by sub_795C40 0x795C40
  /* 0x03E */ unsigned __int8 m_b3E;   // set by Btl 294 on a unit and its two weapon records, cleared by sub_793480. sub_796DC0 0x796E28 reads it
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
  /* 0x3F0 */ unsigned __int8 m_b3F0;   // Written by Btl_OnUnitChrReloaded 0x783BC0, Btl_SetupUnitRoster 0x79C110, sub_7B7D70 0x7B7D70 and 4 more. Read by sub_7AF320 0x7AF320, sub_78AC20 0x78AC20, sub_787840 0x787840 and 8 more
  /* 0x3F1 */ unsigned __int8 m_b3F1;   // Written by Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_031_resi 0x7A5F80. Read by sub_7A8C80 0x7A8C80
  /* 0x3F2 */ unsigned __int8 m_b3F2;   // Written by sub_788480 0x788480, Btl_SetupUnitRoster 0x79C110, sub_7AA160 0x7AA160 and 3 more. Read by sub_7AA160 0x7AA160
  /* 0x3F3 */ unsigned __int8 m_b3F3;   // Written by sub_7882C0 0x7882C0, AtelSys_Btl_163_resi 0x7A3730, sub_7883F0 0x7883F0 and 1 more. Read by AtelSys_Btl_008_poll 0x7A3980, AtelSys_Btl_022_resi 0x7A2A20, sub_7A8FC0 0x7A8FC0 and 2 more
  /* 0x3F4 */ unsigned __int8 m_b3F4;   // Written by sub_78CCA0 0x78CCA0, Btl_ResetUnitForReentry 0x78D530. Read by sub_795A60 0x795A60
  /* 0x3F5 */ unsigned __int8 m_leaveOnDeath;   // 3 for enemies, 0 otherwise (FFX_Btl_SetupUnitRoster). When non-zero FFX_Btl_OnUnitDefeated clears
                                                // m_onField on death, so a dead enemy leaves the field and a dead character stays (BATTLE_START_AND_END.md 2.4).
                                                // FFX_Btl_ResetUnitForReentry restores it from m_leaveOnDeathDefault. AI property 79
  /* 0x3F6 */ unsigned __int8 m_leaveOnDeathDefault;   // the value FFX_Btl_ResetUnitForReentry copies back into m_leaveOnDeath. Btl 078 writes both
  /* 0x3F7 */ unsigned __int8 m_b3F7;   // Written by sub_79E3D0 0x79E3D0. Read by sub_78CCA0 0x78CCA0
  /* 0x3F8 */ unsigned __int8 m_b3F8;   // AI property 297. Written by Btl_SetupUnitRoster 0x79C110, Btl_SwapUnitIntoPartySlot 0x7ADAE0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_796D70 0x796D70, sub_7DBEE0 0x7DBEE0
  /* 0x3F9 */ unsigned __int8 m_b3F9;   // 1 for the aeon units 8..17 at roster setup. Read by FFX_Btl_QueueAeonTransition and Btl 279. AI property 330
  /* 0x3FA */ unsigned __int8 m_b3FA;   // Written by Btl_StartCommandExec 0x7AE370. Read by sub_7AB360 0x7AB360
  /* 0x3FB */ unsigned __int8 m_b3FB;   // AI property 340. Written by sub_7AB360 0x7AB360, BtlAi_SetProperty 0x7B4B70. Read by sub_7A88E0 0x7A88E0, BtlAi_GetProperty 0x7B2DC0, sub_7B4A70 0x7B4A70
  /* 0x3FC */ unsigned __int16 m_motionSpeed;   // motion speed factor, sub_78AC20 0x78ACCB and sub_7A8FC0 scale frame counts by it / 8. 256 at setup,
                                                // Btl 058 writes it, sub_7882C0 hands it to sub_788430
  unsigned __int8 _unk_3FE[0x2];
  /* 0x400 */ unsigned __int16 m_w400;   // AI property 251. Written by Btl_OnUnitChrReloaded 0x783BC0, sub_78AC20 0x78AC20, Btl_SetupUnitRoster 0x79C110 and 1 more. Read by BtlAi_GetProperty 0x7B2DC0, sub_78AC20 0x78AC20, sub_787840 0x787840 and 5 more
  /* 0x402 */ unsigned __int16 m_w402;   // Written by sub_788430 0x788430, sub_7AA260 0x7AA260, sub_7AA280 0x7AA280. Read by sub_78AC20 0x78AC20, sub_7A8FC0 0x7A8FC0
  /* 0x404 */ unsigned __int8 m_b404;   // Written by sub_788430 0x788430, sub_793E00 0x793E00, sub_788E90 0x788E90 and 3 more. Read by sub_78AC20 0x78AC20, sub_7882C0 0x7882C0, sub_7A8FC0 0x7A8FC0
  /* 0x405 */ unsigned __int8 m_b405;   // AI property 252. Written by sub_787C30 0x787C30, sub_788480 0x788480, sub_7A8FC0 0x7A8FC0 and 2 more. Read by BtlAi_GetProperty 0x7B2DC0, sub_7A8FC0 0x7A8FC0
  /* 0x406 */ unsigned __int8 m_b406;   // Written by sub_78AC20 0x78AC20, Btl_SetupUnitRoster 0x79C110. Read by sub_78AC20 0x78AC20, sub_7A8FC0 0x7A8FC0, sub_7DBEE0 0x7DBEE0
  /* 0x407 */ unsigned __int8 m_b407;   // Written by AtelSys_Btl_265_resi 0x7A3DF0. Read by sub_784420 0x784420
  /* 0x408 */ unsigned __int8 m_b408;   // AI property 229. Written by Btl_SetupUnitRoster 0x79C110, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by sub_7A8C80 0x7A8C80, BtlAi_GetProperty 0x7B2DC0
  /* 0x409 */ unsigned __int8 m_b409;   // AI property 269. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7A8C80 0x7A8C80
  /* 0x40A */ unsigned __int8 m_b40A;   // AI property 123. Written by sub_7882C0 0x7882C0, sub_7A8FC0 0x7A8FC0, BtlAi_SetProperty 0x7B4B70. Read by sub_792880 0x792880, BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0
  /* 0x40B */ unsigned __int8 m_b40B;   // No writer found. Read by sub_792880 0x792880
  /* 0x40C */ unsigned __int8 m_attackNormalFrame;   // script stat stat_attack_normal_frame (SG_DebugWin_WriteMotionMacro 0x7DCF70 writes the macro, the CHR data 2
                                                     // debug window edits it). AI property 124
  /* 0x40D */ unsigned __int8 m_attackNearFrame;   // script stat stat_attack_near_frame (SG_DebugWin_WriteMotionMacro). AI property 278
  /* 0x40E */ unsigned __int8 m_attackMotionFrame;   // script stat stat_attack_motion_frame (SG_DebugWin_WriteMotionMacro). AI property 291
  /* 0x40F */ unsigned __int8 m_b40F;   // Written by Btl_SetupUnitRoster 0x79C110, sub_79FD20 0x79FD20, sub_793E00 0x793E00. Read by sub_79FE80 0x79FE80
  /* 0x410 */ unsigned __int8 m_b410;   // Written by sub_79FE80 0x79FE80. Read by sub_79FE80 0x79FE80
  /* 0x411 */ unsigned __int8 m_b411;   // Written by AtelSys_Btl_269_resi 0x7A3C20, sub_7AF920 0x7AF920, sub_793E00 0x793E00. No reader found
  /* 0x412 */ unsigned __int8 m_b412;   // AI property 93. Written by maybe_Btl_SetUnitFlag1042 0x796110, sub_788E90 0x788E90, BtlAi_SetProperty 0x7B4B70. Read by AtelSys_Btl_048_resi 0x7A5090, sub_7AA160 0x7AA160, BtlAi_GetProperty 0x7B2DC0 and 1 more
  /* 0x413 */ unsigned __int8 m_b413;   // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788480 0x788480, maybe_Btl_EffectSetReady 0x788200, sub_7DBEE0 0x7DBEE0 and 1 more
  /* 0x414 */ unsigned __int8 m_b414;   // Written by sub_793E00 0x793E00, sub_788F90 0x788F90. Read by sub_7AB360 0x7AB360, sub_7DBEE0 0x7DBEE0
  /* 0x415 */ unsigned __int8 m_moveKind;   // scripted move kind, 0 idle. FFX_Btl_StartUnitMove sets it from its argument & 0x3F, sub_7AB010 0x7AB010 sets 7.
                                            // FFX_Btl_StepUnitMove 0x7AA2A0 steps it: 7 follows a spline from 0x1136428, 16 orbits in polar form, bit 0x80
                                            // re-aims every frame. Cleared with m_moveActive when m_moveFrame reaches m_moveTotalFrames. AI property 331
  /* 0x416 */ unsigned __int8 m_moveArg;   // FFX_Btl_StartUnitMove argument 3, passed back unchanged when FFX_Btl_StepUnitMove restarts the move. AI property 90
  /* 0x417 */ unsigned __int8 m_b417;   // Written by sub_7AB010 0x7AB010, AtelSys_Btl_187_resi 0x7A4CF0, FFX_Btl_StartUnitMove 0x7AB540 and 1 more. No reader found
  /* 0x418 */ unsigned __int8 m_moveActive;   // cleared with m_moveKind when a move ends (FFX_Btl_StepUnitMove)
  /* 0x419 */ unsigned __int8 m_moveUseGravity;   // 0xFF when FFX_Btl_StartUnitMove got a non-zero gravity, then FFX_Btl_StepUnitMove adds the parabolic arc
  /* 0x41A */ unsigned __int8 m_b41A;   // Written by sub_78CA20 0x78CA20. Read by sub_78CA20 0x78CA20
  /* 0x41B */ unsigned __int8 m_directionFixFlag;   // script stat stat_direction_fix_flag (SG_DebugWin_WriteMotionMacro), the CHR data 1 debug window labels it "DIR : Fix". AI property 164
  /* 0x41C */ unsigned __int8 m_b41C;   // AI property 162. Written by sub_793CF0 0x793CF0, sub_787950 0x787950, sub_7B7D70 0x7B7D70 and 12 more. Read by BtlAi_GetProperty 0x7B2DC0, Btl_EndPhaseStep 0x7917D0, sub_7DBEE0 0x7DBEE0 and 1 more
  /* 0x41D */ unsigned __int8 m_directionChangeEffect;   // script stat stat_direction_change_effect (SG_DebugWin_WriteMotionMacro), debug label "DIR : Return". 1 at setup. AI property 163
  /* 0x41E */ unsigned __int8 m_b41E;   // AI property 125. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_794DB0 0x794DB0, BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0
  /* 0x41F */ unsigned __int8 m_b41F;   // AI property 126. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_794D70 0x794D70, BtlAi_GetProperty 0x7B2DC0
  /* 0x420 */ unsigned __int8 m_b420;   // AI property 91. Written by sub_7B78C0 0x7B78C0, maybe_Btl_SetUnitFlag1056 0x7960D0, AtelSys_Btl_028_resi 0x7A59D0 and 2 more. Read by sub_7B78C0 0x7B78C0, BtlAi_GetProperty 0x7B2DC0, sub_795640 0x795640 and 1 more
  unsigned __int8 _unk_421[0x1];
  /* 0x422 */ unsigned __int8 m_linkUnit;   // own index at roster setup, AI property 89 points it at a target selector result. FFX_Btl_ResolveTargets
                                            // 0x792062 and sub_78CA20 0x78CA33 read it, Btl 215 tests its KO
  /* 0x423 */ unsigned __int8 m_b423;   // AI property 169. Written by maybe_Btl_SetupUnitChr 0x793670, sub_796550 0x796550, BtlAi_SetProperty 0x7B4B70. Read by maybe_Btl_SetupUnitChr 0x793670, Btl_OnUnitChrLoaded 0x7839B0, BtlAi_GetProperty 0x7B2DC0 and 1 more
  /* 0x424 */ unsigned __int8 m_b424;   // AI property 233. Written by sub_7842F0 0x7842F0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0
  /* 0x425 */ unsigned __int8 m_savedPosValid;   // set by sub_78F040 0x78F040 with m_savedPos, cleared by sub_793E00 and sub_78D620. Btl 157 waits on it. AI property 171
  /* 0x426 */ unsigned __int8 m_b426;   // AI property 118. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0, sub_78D680 0x78D680 and 1 more
  /* 0x427 */ unsigned __int8 m_b427;   // Written by sub_7AA160 0x7AA160. Read by sub_7AA160 0x7AA160
  /* 0x428 */ unsigned __int8 m_b428;   // Written by sub_7AA160 0x7AA160. Read by sub_7AA160 0x7AA160
  /* 0x429 */ unsigned __int8 m_b429;   // Written by Btl_OnMotionEvent 0x7B74D0, sub_7882C0 0x7882C0, sub_7AB360 0x7AB360. Read by sub_789030 0x789030, Btl_OnMotionEvent 0x7B74D0, sub_7DBEE0 0x7DBEE0
  /* 0x42A */ unsigned __int8 m_b42A;   // Written by sub_7AB010 0x7AB010. Read by FFX_Btl_StepUnitMove 0x7AA2A0
  /* 0x42B */ unsigned __int8 m_b42B;   // Written by sub_795E70 0x795E70, sub_795EB0 0x795EB0, sub_7AB170 0x7AB170. Read by sub_7A9710 0x7A9710
  /* 0x42C */ unsigned __int8 m_b42C;   // Written by sub_795EB0 0x795EB0. Read by sub_7A9710 0x7A9710
  /* 0x42D */ unsigned __int8 m_b42D;   // Written by sub_7A9710 0x7A9710. Read by sub_7A9710 0x7A9710
  /* 0x42E */ unsigned __int8 m_b42E;   // Written by FFX_Btl_StepUnitMove 0x7AA2A0, Btl_PlaceArrivedAeon 0x7AC6A0, sub_795EB0 0x795EB0 and 2 more. Read by FFX_Btl_StepUnitMove 0x7AA2A0, sub_795E70 0x795E70, Btl_EndPhaseStep 0x7917D0 and 1 more
  /* 0x42F */ unsigned __int8 m_b42F;   // Written by sub_795EB0 0x795EB0. Read by FFX_Btl_StepUnitMove 0x7AA2A0
  /* 0x430 */ unsigned __int8 m_b430;   // Written by sub_795EB0 0x795EB0. Read by FFX_Btl_StepUnitMove 0x7AA2A0
  /* 0x431 */ unsigned __int8 m_b431;   // Written by sub_795EB0 0x795EB0. Read by sub_7A9710 0x7A9710
  /* 0x432 */ unsigned __int8 m_b432;   // Written by sub_7AB360 0x7AB360, AtelSys_Btl_049_resi 0x7A2D00, AtelSys_Btl_018_resi 0x7A29A0 and 3 more. Read by sub_7AF320 0x7AF320, sub_7DBEE0 0x7DBEE0, sub_78AC20 0x78AC20 and 11 more
  /* 0x433 */ unsigned __int8 m_b433;   // AI property 165. Written by sub_78CA20 0x78CA20, Btl_OnUnitChrLoaded 0x7839B0, BtlAi_SetProperty 0x7B4B70 and 2 more. Read by BtlAi_GetProperty 0x7B2DC0, Btl_ExecCommand 0x792210, sub_784220 0x784220 and 2 more
  /* 0x434 */ unsigned __int8 m_b434;   // Written by sub_7A8FC0 0x7A8FC0, sub_7882C0 0x7882C0. Read by sub_7878A0 0x7878A0, Btl_FinishActions 0x7911E0, AtelSys_Btl_163_resi 0x7A3730 and 2 more
  /* 0x435 */ unsigned __int8 m_b435;   // Written by AtelSys_Btl_018_resi 0x7A29A0, Btl_ExecCommand 0x792210. Read by sub_788480 0x788480, sub_7B0520 0x7B0520
  /* 0x436 */ unsigned __int8 m_b436;   // Written by sub_78DC30 0x78DC30, sub_7B7E20 0x7B7E20, Btl_SwapUnitIntoPartySlot 0x7ADAE0 and 1 more. Read by sub_788480 0x788480, sub_787950 0x787950, Btl_ExecCommand 0x792210 and 9 more
  /* 0x437 */ unsigned __int8 m_b437;   // AI property 335. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by sub_7B1E80 0x7B1E80, BtlAi_GetProperty 0x7B2DC0
  /* 0x438 */ unsigned __int8 m_b438;   // Written by sub_7B1E80 0x7B1E80, AtelSys_Btl_258_resi 0x7A35D0, sub_78DC30 0x78DC30 and 2 more. Read by sub_7AAB80 0x7AAB80, sub_7B1F10 0x7B1F10
  /* 0x439 */ unsigned __int8 m_b439;   // Written by sub_7B1F10 0x7B1F10, sub_7B1E80 0x7B1E80, AtelSys_Btl_258_resi 0x7A35D0 and 1 more. Read by sub_7AAB80 0x7AAB80
  /* 0x43A */ unsigned __int8 m_b43A;   // Written by Btl_EndPhaseStep 0x7917D0. Read by Btl_EndPhaseStep 0x7917D0
  /* 0x43B */ unsigned __int8 m_b43B;   // AI property 220. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_EndPhaseStep 0x7917D0
  /* 0x43C */ unsigned __int8 m_b43C;   // AI property 245. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0
  /* 0x43D */ unsigned __int8 m_b43D;   // AI property 134. Written by Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_265_resi 0x7A3DF0, Btl_SwapUnitIntoPartySlot 0x7ADAE0 and 1 more. Read by Btl_OnUnitChrLoaded 0x7839B0, BtlAi_GetProperty 0x7B2DC0, maybe_Btl_SetupUnitChr 0x793670
  /* 0x43E */ unsigned __int8 m_b43E;   // AI property 94. Written by sub_7A89C0 0x7A89C0, BtlAi_SetProperty 0x7B4B70, Btl_ResetUnitForReentry 0x78D530 and 1 more. Read by BtlAi_GetProperty 0x7B2DC0, sub_78CA20 0x78CA20, sub_7DBEE0 0x7DBEE0 and 1 more
  /* 0x43F */ unsigned __int8 m_attachParentUnit;   // unit this one rides on, 0xFF none. sub_796EA0 0x796EA0 takes the position from the parent's bone
                                                    // m_attachBone, sub_7AAB80 0x7AAB80 calls FFX_Ch_AttachToParentBone. Btl 082 writes all three
  /* 0x440 */ unsigned __int8 m_hasAttachedUnits;   // set on a parent. The visibility, alpha and colour passes (FFX_Btl_SetUnitShowVote, sub_7962D0, sub_796480) then
                                                    // recurse into every unit whose m_attachParentUnit is this one
  /* 0x441 */ unsigned __int8 m_attachBone;   // parent bone index for m_attachParentUnit (FFX_Ch_GetBoneWorldPos, FFX_Ch_AttachToParentBone)
  /* 0x442 */ unsigned __int8 m_b442;   // AI property 338. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_79F380 0x79F380
  unsigned __int8 _unk_443[0x3];
  /* 0x446 */ unsigned __int8 m_b446;   // AI property 224. Written by Btl_CmdQueue_RemoveAt 0x7B0860, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_78D8D0 0x78D8D0, sub_7B75C0 0x7B75C0 and 4 more
  /* 0x447 */ unsigned __int8 m_b447;   // AI properties 119..120. Written by BtlAi_SetProperty 0x7B4B70, Btl_SetupUnitRoster 0x79C110. Read by BtlAi_SetProperty 0x7B4B70, BtlAi_GetProperty 0x7B2DC0, sub_78C270 0x78C270
  /* 0x448 */ unsigned __int8 m_b448;   // AI property 122. Written by sub_7AB360 0x7AB360, sub_7882C0 0x7882C0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7AB360 0x7AB360, sub_7DBEE0 0x7DBEE0
  /* 0x449 */ unsigned __int8 m_b449;   // Written by sub_787950 0x787950, Btl_ResetUnitForReentry 0x78D530. Read by sub_78DC30 0x78DC30
  /* 0x44A */ unsigned __int8 m_b44A;   // AI property 247. Written by Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70, sub_7AF230 0x7AF230. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B75C0 0x7B75C0
  /* 0x44B */ unsigned __int8 m_b44B;   // AI property 250. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7AF230 0x7AF230
  /* 0x44C */ unsigned __int8 m_b44C;   // Written by BtlAi_SetUnitFloat 0x7B4A90. Read by maybe_Btl_SetupUnitChr 0x793670
  /* 0x44D */ unsigned __int8 m_b44D;   // Written by BtlAi_SetProperty 0x7B4B70. Read by maybe_Btl_SetupUnitChr 0x793670
  /* 0x44E */ unsigned __int8 m_b44E;   // AI property 161. Written by sub_78CA20 0x78CA20, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, AtelSys_Btl_013_resi 0x7A63F0
  /* 0x44F */ unsigned __int8 m_b44F;   // AI property 289. Written by sub_787950 0x787950, Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_038_resi 0x7A5D80 and 3 more. Read by BtlAi_GetProperty 0x7B2DC0, AtelSys_Btl_039_resi 0x7A5E90, sub_7DBEE0 0x7DBEE0
  /* 0x450 */ unsigned __int8 m_b450;   // Written by sub_788480 0x788480, sub_787950 0x787950, Btl_FinishActions 0x7911E0. Read by sub_78D7F0 0x78D7F0
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
  /* 0x4B4 */ unsigned __int8 m_b4B4;   // AI property 253. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7AF0F0 0x7AF0F0
  /* 0x4B5 */ unsigned __int8 m_b4B5;   // 1 on units 28..30 when FFX_Btl_SetupWeaponChr hands them a weapon CHR. sub_793330 and sub_793F10 need it
  /* 0x4B6 */ unsigned __int8 m_b4B6[2];   // two flags indexed by side record, set by sub_793F10 0x793F86, read by sub_793330 and sub_791170
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
  /* 0x4E4 */ float m_f4E4;   // the last Btl 178 float. sub_795A20 0x795A20 returns it times m_scale[2]
  /* 0x4E8 */ float m_f4E8;   // AI property 261. Written by maybe_Btl_SetupUnitChr 0x793670, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_79F380 0x79F380
  /* 0x4EC */ float m_f4EC;   // AI property 296. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_795250 0x795250, sub_7DBEE0 0x7DBEE0
  /* 0x4F0 */ float m_f4F0;   // Written by AtelSys_Btl_142_resi 0x7A5C20, Btl_SetupUnitRoster 0x79C110. Read by AtelSys_Btl_142_resi 0x7A5C20, sub_7A9230 0x7A9230
  /* 0x4F4 */ float m_f4F4;   // Written by FFX_Btl_StepUnitMove 0x7AA2A0. Read by sub_7A9710 0x7A9710
  /* 0x4F8 */ void *m_motionTable;   // pointer into the motion table at dword_112A954 for m_motionNum (sub_79CD80), read by sub_79CB40 0x79CB40
  /* 0x4FC */ char m_placeLayout;   // the battle position key (layout, group, slot) sub_7ABFE0 0x7ABFE0 turns into a position
                                    // (maybe_FFX_Btl_GetUnitTargetPos). AI property 28
  /* 0x4FD */ char m_placeGroup;   // 3 characters, 4 aeons, 5 enemies at roster setup. FFX_Btl_PartyFlee adds 3
  /* 0x4FE */ char m_placeSlot;   // battle party slot 0..2, aeon index, or the running enemy index (FFX_Btl_SetupUnitRoster). AI property 29
  /* 0x4FF */ char m_protectLevel;   // target protection level, compared in FFX_Btl_ResolveTargets 0x79202D (BATTLE_COMMAND.md). AI property 30
  /* 0x500 */ char m_savedPlace[3];   // m_placeLayout, m_placeGroup and m_placeSlot saved by FFX_Btl_SwapUnitIntoPartySlot and restored by
                                      // FFX_Btl_ReturnPartyAfterSummon
  unsigned __int8 _unk_503[0x1];
  /* 0x504 */ unsigned __int8 m_b504;   // AI property 31. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_795040 0x795040, BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0
  /* 0x505 */ unsigned __int8 m_b505;   // AI property 26. Written by AtelSys_Btl_265_resi 0x7A3DF0, BtlAi_SetProperty 0x7B4B70, AtelSys_Btl_041_resi 0x7A3FB0. Read by sub_79E8C0 0x79E8C0, maybe_Btl_GetUnitCenterPos 0x794C20, AtelSys_Btl_142_resi 0x7A5C20 and 6 more
  /* 0x506 */ unsigned __int8 m_b506;   // AI property 343. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, maybe_Btl_SetupUnitChr 0x793670
  unsigned __int8 _unk_507[0x1];
  /* 0x508 */ unsigned __int8 m_b508;   // AI property 95. Written by Btl_OnUnitChrReloaded 0x783BC0, sub_7AA100 0x7AA100, AtelSys_Btl_047_resi 0x7A5140 and 4 more. Read by AtelSys_Btl_142_resi 0x7A5C20, sub_7AF8A0 0x7AF8A0, sub_78AC20 0x78AC20 and 17 more
  /* 0x509 */ char m_altPlace[3];   // a second (layout, group, slot) key. sub_7AC600 0x7AC600 copies it into the live one. AI properties 85..86
  /* 0x50C */ unsigned __int8 m_b50C;   // Written by AtelSys_Btl_010_resi 0x7A4100, Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_265_resi 0x7A3DF0. Read by sub_7AB300 0x7AB300
  /* 0x50D */ unsigned __int8 m_b50D;   // AI property 96. Written by sub_7AB360 0x7AB360, sub_78D680 0x78D680, sub_7882C0 0x7882C0 and 1 more. Read by maybe_Btl_GetUnitTargetPos 0x7948E0, maybe_Btl_GetUnitYOffset 0x794990, BtlAi_GetProperty 0x7B2DC0 and 2 more
  /* 0x50E */ unsigned __int8 m_b50E;   // AI property 97. Written by sub_78CA20 0x78CA20, BtlAi_SetProperty 0x7B4B70, Btl_TurnEndStatusTick 0x7AF390 and 1 more. Read by sub_7DBEE0 0x7DBEE0, BtlAi_GetProperty 0x7B2DC0, Btl_ExecCommand 0x792210
  /* 0x50F */ unsigned __int8 m_b50F;   // Written by sub_78CA20 0x78CA20, sub_78C0F0 0x78C0F0. Read by sub_78CA20 0x78CA20
  /* 0x510 */ unsigned __int8 m_b510;   // Written by sub_793E00 0x793E00, sub_795FF0 0x795FF0. Read by sub_7958C0 0x7958C0, sub_7DBEE0 0x7DBEE0
  /* 0x511 */ char m_facingIndex;   // index into m_homeFacing and m_homeFacing2
  /* 0x512 */ unsigned __int8 m_b512;   // Written by AtelSys_Btl_050_resi 0x7A6F70. Read by sub_793CF0 0x793CF0
  unsigned __int8 _unk_513[0x1];
  /* 0x514 */ float m_homeFacing[4];   // facing per position, picked by m_facingIndex (sub_7948A0 0x7948A0, magic host API 169, returns the
                                       // current one). sub_793260 adds pi to it for m_returnFacing
  /* 0x524 */ float m_homeFacing2[4];   // a second value per position, read with m_homeFacing by sub_794060 0x794060 case 6
  /* 0x534 */ float m_yOffset;   // height offset added to the target point (maybe_FFX_Btl_GetUnitTargetPos, maybe_FFX_Btl_GetUnitYOffset).
                                 // sub_7AB300 0x7AB300 sets it to ground - height / 2 + m_yOffsetBase
  /* 0x538 */ float m_yOffsetBase;   // written by Btl 010 and Btl 265, read by sub_7AB300
  /* 0x53C */ float m_f53C;   // Written by sub_795FF0 0x795FF0. Read by sub_7958C0 0x7958C0
  /* 0x540 */ char m_battleName[40];   // the name shown in battle, 40 bytes of BTLCHR.m_name for a monster (FFX_Btl_LoadUnitParams) or the
                                       // character name. Btl 220 writes it
  unsigned __int8 _unk_568[0x28];   // no reader or writer found. FFX_Btl_LoadUnitParams 0x79B4F0 zeroes it with the rest of 0x540..0x72B.
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
  /* 0x5C0 */ unsigned __int8 m_b5C0;   // Written by sub_7B1AA0 0x7B1AA0, Btl_CheckCommandCost 0x78AB20, sub_7AFBD0 0x7AFBD0. Read by sub_79E8C0 0x79E8C0, sub_7B1AA0 0x7B1AA0
  /* 0x5C1 */ unsigned __int8 m_b5C1;   // Written by Btl_LoadCharRecordIntoActor 0x79C5F0, Btl_LoadUnitParams 0x79B4F0. Read by Btl_CtbPreview_ApplyPendingCmd 0x7898D0, sub_78CD30 0x78CD30, Btl_ComputeEffectForTarget 0x78E630
  /* 0x5C2 */ unsigned __int8 m_b5C2;   // Written by Btl_LoadUnitParams 0x79B4F0, sub_7A89C0 0x7A89C0. Read by Btl_OnUnitChrLoaded 0x7839B0, sub_7A89C0 0x7A89C0, sub_79AE30 0x79AE30 and 1 more
  /* 0x5C3 */ unsigned __int8 m_b5C3;   // AI property 259. Written by Btl_OnUnitChrLoaded 0x7839B0, Btl_LoadUnitParams 0x79B4F0, sub_7A89C0 0x7A89C0 and 1 more. Read by sub_7A89C0 0x7A89C0, BtlAi_GetProperty 0x7B2DC0, sub_79AE10 0x79AE10 and 1 more
  /* 0x5C4 */ unsigned __int8 m_provoker;   // the provoking unit, the forced target of status 1 bit 0x400. AI property 257
  /* 0x5C5 */ unsigned __int8 m_threatener;   // the threatening unit (on the target), sub_78E460 0x78E460
  /* 0x5C6 */ unsigned __int8 m_threatTarget;   // the threatened unit (on the threatener), sub_78E410 0x78E410
  /* 0x5C7 */ unsigned __int8 m_b5C7;   // Written by Btl_LoadCharRecordIntoActor 0x79C5F0. Read by Btl_CtbPreview_ApplyPendingCmd 0x7898D0, Btl_ComputeEffectForTarget 0x78E630
  /* 0x5C8 */ unsigned __int8 m_doomCount;   // doom countdown, live (sub_799CD0). AI property 160
  /* 0x5C9 */ unsigned __int8 m_doomStart;   // doom start count, copied to m_doomCount when Doom lands. AI property 159
  /* 0x5CA */ unsigned __int8 m_autoLifeArmed;   // set with status 2 bit 0x200, Auto-Life (sub_78B4E0, sub_78C580, sub_7B2910 0x7B2910)
  /* 0x5CB */ unsigned __int8 m_b5CB;   // AI property 256. Written by BtlAi_SetProperty 0x7B4B70. Read by AtelSys_Btl_011_AiAddCommand_resi 0x7A44B0, sub_799D50 0x799D50, BtlAi_GetProperty 0x7B2DC0
  /* 0x5CC */ int m_totalAp;   // total AP, written by Btl_LoadCharRecordIntoActor. No reader found
  /* 0x5D0 */ int m_hp;   // current HP. AI property 242
  /* 0x5D4 */ int m_mp;   // current MP. AI property 1
  /* 0x5D8 */ unsigned __int8 m_b5D8;   // Written by Btl_LoadCharRecordIntoActor 0x79C5F0. Read by Btl_LoadCharRecordIntoActor 0x79C5F0, sub_789690 0x789690
  /* 0x5D9 */ unsigned __int8 m_elemAbsorb;   // element absorb bits (auto-ability row +17, monster param +43). The five element bytes 0x5D9..0x5DD
                                              // hold one bit per element (AI properties 98..117)
  /* 0x5DA */ unsigned __int8 m_elemImmune;   // element immunity bits (row +18). AI properties 98..102
  /* 0x5DB */ unsigned __int8 m_elemHalf;   // element half damage bits (row +19). AI properties 103..107
  /* 0x5DC */ unsigned __int8 m_elemWeak;   // element weakness bits (row +20). AI properties 108..112
  /* 0x5DD */ unsigned __int8 m_elemAttack;   // attack element bits (row +21), ORed into the ability's element by sub_78CD30 0x78CD61. AI properties 113..117
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
  /* 0x63D */ unsigned __int8 m_b63D;   // AI property 8 (set). sub_795840 0x795840 returns it
  /* 0x63E */ unsigned __int8 m_sosState;   // SOS state written by sub_78D7F0 0x78D7F0
  /* 0x63F */ unsigned __int8 m_sosStatePrev;   // compared with m_sosState by sub_7AF260
  /* 0x640 */ unsigned __int8 m_derivedFlags;   // derived status flags, bit 4 makes the MP cost 0 (FFX_Btl_GetRawMpCost 0x78D013)
  /* 0x641 */ unsigned __int8 m_statusResist[25];   // per status resistance, 0xFF immune (row +60, monster param +47, AI properties 175..199)
  /* 0x65A */ unsigned __int16 m_status2Immune;   // status 2 immunity mask (row +96, monster param +78, AI properties 200..214)
  /* 0x65C */ unsigned __int8 m_ctb;   // CTB counter, 0 means ready (FFX_Btl_CtbTick, BATTLE_CTB_AND_STATUS.md 1.1). AI property 232
  /* 0x65D */ unsigned __int8 m_ctbBase;   // 3 * tick, the CTB base and reload value
  /* 0x65E */ unsigned __int8 m_b65E;   // Written by sub_79A190 0x79A190. Read by Btl_DamageFormula 0x789BF0, sub_7DBEE0 0x7DBEE0
  /* 0x65F */ unsigned __int8 m_b65F;   // No writer found. Read by sub_78A890 0x78A890, sub_7DBEE0 0x7DBEE0
  /* 0x660 */ unsigned __int8 m_b660;   // No writer found. Read by Btl_DamageFormula 0x789BF0, sub_7DBEE0 0x7DBEE0
  /* 0x661 */ unsigned __int8 m_b661;   // No writer found. Read by sub_78A890 0x78A890, sub_7DBEE0 0x7DBEE0
  /* 0x662 */ unsigned __int8 m_b662;   // Written by sub_79A190 0x79A190. Read by sub_78A890 0x78A890, sub_7DBEE0 0x7DBEE0, sub_789690 0x789690
  /* 0x663 */ unsigned __int8 m_b663;   // No writer found. Read by sub_78A890 0x78A890, sub_7DBEE0 0x7DBEE0, sub_789690 0x789690
  /* 0x664 */ unsigned __int16 m_abilityBits[22];   // usable ability bitfield, bit (id & 0xF) of word (id & 0xFFF) / 16. sub_79BB50 0x79BB50 fills it
                                                    // from the character record words +62..+72 and the save block at 0x11307FC, sub_79C070 0x79C070 sets bits
  /* 0x690 */ unsigned __int16 m_sealedAbilities[22];   // sealed ability bitfield, same indexing (FFX_Btl_IsCommandLocked, sub_79B470 0x79B470)
  /* 0x6BC */ unsigned __int16 m_autoAbility[3];   // the 48 bit auto-ability effect mask (row +98/+100/+102). Word 0 bit 2 First Strike, word 2 bit 2
                                                   // Capture (BATTLE_CTB_AND_STATUS.md 1.1, MONSTER_ARENA.md). AI properties 58..63, 66..71, 74, 76..78
  unsigned __int8 _unk_6C2[0x4];
  /* 0x6C6 */ unsigned __int16 m_forcedCommand;   // forced command id for the forced action paths (sub_799D50 0x799D50). 0x3000 for a character,
                                                  // monster param +112
  /* 0x6C8 */ unsigned __int16 m_escapeCommand;   // this unit's escape command id (FFX_Btl_GetEscapeCommandId 0x78FBA4)
  /* 0x6CA */ unsigned __int8 m_b6CA;   // Written by sub_78D980 0x78D980, Btl_StartCommandExec 0x7AE370. Read by sub_78D980 0x78D980
  /* 0x6CB */ unsigned __int8 m_queuedAiCount;   // number of entries in m_queuedAiCommand, at most 4 (sub_7AC840 0x7AC840)
  /* 0x6CC */ unsigned __int8 m_mpCost;   // resolved MP cost, cached by FFX_Btl_CheckCommandCost (AI property 310)
  /* 0x6CD */ unsigned __int8 m_overdriveCost;   // resolved overdrive cost (AI property 311)
  /* 0x6CE */ unsigned __int8 m_b6CE;   // AI property 258. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_GetRawMpCost 0x78CF70
  /* 0x6CF */ unsigned __int8 m_b6CF;   // AI property 312. Written by BtlAi_SetProperty 0x7B4B70, Btl_LatchOverdriveCost 0x7B03E0. Read by BtlAi_GetProperty 0x7B2DC0, Btl_LatchOverdriveCost 0x7B03E0
  /* 0x6D0 */ unsigned __int8 m_b6D0;   // AI property 313. Written by Btl_FinishActions 0x7911E0, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_OnUnitDefeated 0x78C740, Btl_StartCommandExec 0x7AE370
  /* 0x6D1 */ unsigned __int8 m_summoner;   // the summoning unit, -1 none (sub_788E10 0x788E10). AI property 262
  /* 0x6D2 */ unsigned __int8 m_regenTicks;   // CTB ticks since the last regen heal, saturates at 0xFF
  /* 0x6D3 */ unsigned __int8 m_b6D3;   // Written by Btl_LoadUnitParams 0x79B4F0, sub_78BA70 0x78BA70, sub_7AF880 0x7AF880. Read by sub_7AF920 0x7AF920
  /* 0x6D4 */ unsigned __int8 m_b6D4;   // Written by Btl_SetupUnitRoster 0x79C110, sub_78BA70 0x78BA70. Read by sub_7DBEE0 0x7DBEE0, sub_7AF800 0x7AF800, sub_7AF8A0 0x7AF8A0 and 1 more
  unsigned __int8 _unk_6D5[0x1];
  /* 0x6D6 */ unsigned __int16 m_captureIndex;   // capture species index, 255 not capturable (Btl_OnUnitDefeated, MONSTER_ARENA.md)
  /* 0x6D8 */ unsigned __int8 m_aeonRecovery;   // aeon recovery countdown (Btl_IsUnitUnsummonable 0x79A0A9)
  /* 0x6D9 */ unsigned __int8 m_aeonRecoveryLength;   // aeon recovery length, incremented by Btl_StartAeonRecovery 0x78E0C4
  /* 0x6DA */ unsigned __int8 m_b6DA;   // Written by sub_7AF4B0 0x7AF4B0, sub_78C530 0x78C530, sub_78A7D0 0x78A7D0. Read by sub_78CA20 0x78CA20
  /* 0x6DB */ unsigned __int8 m_b6DB;   // AI property 246. Written by Btl_ApplyActionToTargets 0x7892E0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0
  /* 0x6DC */ unsigned __int8 m_b6DC;   // Written by sub_78D8D0 0x78D8D0, sub_78C1D0 0x78C1D0. Read by Btl_ApplyPendingEffects 0x78F060
  /* 0x6DD */ unsigned __int8 m_b6DD;   // Written by Btl_ApplyPendingEffects 0x78F060, sub_78BA70 0x78BA70, Btl_OnUnitDefeated 0x78C740 and 1 more. Read by maybe_Btl_GetUnitTargetPos 0x7948E0
  /* 0x6DE */ unsigned __int8 m_forceAttack;   // when set the current command is forced to ability 0x3028 (BATTLE_COMMAND.md)
  /* 0x6DF */ unsigned __int8 m_b6DF;   // AI property 276. Written by Btl_PlaceArrivedAeon 0x7AC6A0, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_StartCommandExec 0x7AE370
  /* 0x6E0 */ unsigned __int8 m_b6E0;   // AI property 277. Written by Btl_ExecCommand 0x792210, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, Btl_ExecCommand 0x792210
  /* 0x6E1 */ unsigned __int8 m_b6E1;   // AI property 279. Written by Btl_LoadUnitParams 0x79B4F0, BtlAi_SetProperty 0x7B4B70, sub_7AFB60 0x7AFB60. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B2B00 0x7B2B00, BtlAi_SetProperty 0x7B4B70 and 1 more
  unsigned __int8 _unk_6E2[0x2];
  /* 0x6E4 */ int m_workHp;   // working HP during damage staging (Btl_CtbPreview_ApplyPendingCmd copies m_hp)
  /* 0x6E8 */ int m_workMp;   // working MP during damage staging
  /* 0x6EC */ int m_workCtb;   // working CTB during damage staging, the current value for CTB damage formulas
  /* 0x6F0 */ int m_overdriveModesCounted;   // per battle bitmask of overdrive modes already counted
  /* 0x6F4 */ int m_i6F4;   // Written by Btl_ComputeOverdriveRefDamage 0x78D790. Read by Btl_GainOverdriveOnHit 0x7B0D50, sub_7B0F80 0x7B0F80, sub_7DBEE0 0x7DBEE0
  /* 0x6F8 */ int m_i6F8;   // Written by sub_79EFF0 0x79EFF0. No reader found
  /* 0x6FC */ int m_i6FC;   // Written by Btl_LoadUnitParams 0x79B4F0, Btl_OnUnitChrReloaded 0x783BC0, sub_79EFF0 0x79EFF0. Read by sub_79EFF0 0x79EFF0
  /* 0x700 */ unsigned __int8 m_b700;   // AI property 294. Written by Btl_OnUnitDefeated 0x78C740, BtlAi_SetProperty 0x7B4B70, Btl_ResetUnitForReentry 0x78D530. Read by BtlAi_GetProperty 0x7B2DC0, sub_7DBEE0 0x7DBEE0
  /* 0x701 */ unsigned __int8 m_b701;   // Written by Btl_RollStatus1AndCounters 0x78AE00, Btl_RollBribe 0x78BDD0. Read by Btl_PayKillRewards 0x7990D0, Btl_OnUnitDefeated 0x78C740, sub_79E3D0 0x79E3D0
  /* 0x702 */ unsigned __int8 m_b702;   // Written by Btl_OnUnitDefeated 0x78C740. Read by Btl_EndPhaseStep 0x7917D0
  unsigned __int8 _unk_703[0x1];
  /* 0x704 */ int m_i704;   // Written by Btl_RollBribe 0x78BDD0. Read by Btl_RollBribe 0x78BDD0
  /* 0x708 */ int m_i708;   // Written by Btl_RollBribe 0x78BDD0. Read by Btl_PayKillRewards 0x7990D0
  /* 0x70C */ float m_overdriveRate;   // overdrive gain multiplier, 1.0 at setup (FFX_Btl_AddOverdrive 0x7B1681)
  unsigned __int8 _unk_710[0x2];
  /* 0x712 */ unsigned __int16 m_w712;   // Written by Btl_LoadUnitParams 0x79B4F0, sub_78B920 0x78B920. Read by sub_78B920 0x78B920
  /* 0x714 */ unsigned __int8 m_b714;   // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. Read by sub_7AFB60 0x7AFB60
  /* 0x715 */ unsigned __int8 m_b715;   // Written by sub_78BF50 0x78BF50. Read by sub_7AFB60 0x7AFB60
  /* 0x716 */ unsigned __int8 m_turnReoffered;   // the turn was handed back after a rejected command (FFX_Btl_ExecCommand 0x792761)
  /* 0x717 */ unsigned __int8 m_b717;   // Written by Btl_CmdQueue_RemoveAt 0x7B0860, sub_7B0520 0x7B0520. Read by Btl_ExecCommand 0x792210, sub_7B0520 0x7B0520
  /* 0x718 */ unsigned __int8 m_b718;   // AI property 137. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_795250 0x795250, sub_7DBEE0 0x7DBEE0
  /* 0x719 */ unsigned __int8 m_b719;   // AI property 136. Written by Btl_LoadUnitParams 0x79B4F0, sub_793E00 0x793E00, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_795250 0x795250, sub_7DBEE0 0x7DBEE0
  /* 0x71A */ unsigned __int16 m_queuedAiCommand[4];   // command ids queued for the unit by sub_7AC840 0x7AC840
  /* 0x722 */ unsigned __int8 m_queuedAiPriority[4];   // priority 0..7 of each m_queuedAiCommand
  /* 0x726 */ unsigned __int8 m_b726;   // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. Read by sub_7AFB60 0x7AFB60
  /* 0x727 */ unsigned __int8 m_b727;   // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. No reader found
  /* 0x728 */ unsigned __int8 m_b728;   // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. Read by sub_7AFB60 0x7AFB60
  /* 0x729 */ unsigned __int8 m_b729;   // Written by AtelSys_Btl_253_resi 0x7A3A10, AtelSys_Btl_254_resi 0x7A3B80. No reader found
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
  /* 0xDC9 */ unsigned __int8 m_bDC9;   // Written by Btl_SwapPartyArrangement 0x7ADE10, Btl_BeginSummon 0x7ADF90. Read by Btl_SwapPartyArrangement 0x7ADE10
  /* 0xDCA */ unsigned __int8 m_bDCA;   // AI property 295. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_785410 0x785410, sub_7B6C50 0x7B6C50, sub_793CF0 0x793CF0 and 1 more
  /* 0xDCB */ unsigned __int8 m_bDCB;   // Written by Btl_SwapUnitIntoPartySlot 0x7ADAE0. Read by Btl_TurnEndStatusTick 0x7AF390, sub_7AAAE0 0x7AAAE0
  /* 0xDCC */ unsigned __int8 m_ko;   // KO. AI property 332
  /* 0xDCD */ unsigned __int8 m_bDCD;   // AI property 173. Written by Btl_SetUnitLeftBattle 0x78DE40, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_ResolveTargetSelector 0x794330, sub_785410 0x785410, sub_7B6C50 0x7B6C50 and 10 more
  /* 0xDCE */ unsigned __int8 m_petrified;   // mirror of status 1 bit 4 (Petrify), rewritten by FFX_Btl_ApplyPendingEffects
  /* 0xDCF */ unsigned __int8 m_bDCF;   // Written by Btl_OnUnitDefeated 0x78C740. Read by sub_793E00 0x793E00
  /* 0xDD0 */ unsigned __int8 m_capturePending;   // capture pending, then the result (Btl_OnUnitDefeated)
  /* 0xDD1 */ unsigned __int8 m_captureResult;   // capture result kept for the AI getter. AI property 342
  /* 0xDD2 */ unsigned __int8 m_bDD2;   // AI property 142. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by Btl_TestBattleOver 0x7928D0, BtlAi_GetProperty 0x7B2DC0, Btl_EndPhaseStep 0x7917D0
  /* 0xDD3 */ unsigned __int8 m_bDD3;   // AI property 172. Written by BtlAi_SetProperty 0x7B4B70. Read by Btl_TestBattleOver 0x7928D0, BtlAi_GetProperty 0x7B2DC0
  /* 0xDD4 */ unsigned __int8 m_bDD4;   // Written by sub_79B470 0x79B470, Btl_SetupUnitRoster 0x79C110. Read by Btl_IsUnitUnsummonable 0x79A080
  /* 0xDD5 */ unsigned __int8 m_bDD5;   // AI property 344. Written by Btl_SetupUnitRoster 0x79C110, AtelSys_Btl_145_resi 0x7A77C0, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, AtelSys_Btl_145_resi 0x7A77C0
  /* 0xDD6 */ unsigned __int8 m_takesTurns;   // takes CTB turns. AI property 81
  /* 0xDD7 */ unsigned __int8 m_inTurnStrip;   // shown in the turn order strip. AI property 83
  /* 0xDD8 */ unsigned __int8 m_bDD8;   // AI property 215. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by sub_785410 0x785410, BtlAi_GetProperty 0x7B2DC0
  /* 0xDD9 */ unsigned __int8 m_bDD9;   // AI property 82. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by sub_7B6C50 0x7B6C50, sub_794CA0 0x794CA0, BtlAi_GetProperty 0x7B2DC0 and 2 more
  /* 0xDDA */ unsigned __int8 m_bDDA;   // AI property 225. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_787C10 0x787C10, sub_787900 0x787900, BtlAi_GetProperty 0x7B2DC0
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
  /* 0xDE9 */ unsigned __int8 m_bDE9;   // Written by sub_78D980 0x78D980, Btl_FinishActions 0x7911E0, Btl_ExecCommand 0x792210. Read by sub_78D980 0x78D980
  /* 0xDEA */ unsigned __int8 m_postActionKind;   // post action kind, copied from command record +6
  /* 0xDEB */ unsigned __int8 m_bDEB;   // AI property 121. Written by BtlAi_SetProperty 0x7B4B70, sub_78B760 0x78B760. Read by BtlAi_GetProperty 0x7B2DC0, sub_78B760 0x78B760
  /* 0xDEC */ unsigned __int8 m_predictedDeath;   // predicted to die from the staged action. AI property 27
  /* 0xDED */ unsigned __int8 m_issuer;   // the unit that issued the current command (FFX_Btl_ApplyPendingEffects)
  /* 0xDEE */ unsigned __int8 m_bDEE;   // Written by Btl_OnUnitDefeated 0x78C740. Read by Btl_EndPhaseStep 0x7917D0
  /* 0xDEF */ unsigned __int8 m_bDEF;   // Written by sub_78CA20 0x78CA20. No reader found
  /* 0xDF0 */ unsigned __int8 m_actionState;   // action state: 0 idle, 1 finishing, 2 acting, 3 done, 4 and 5 waiting (BATTLE_COMMAND.md)
  /* 0xDF1 */ unsigned __int8 m_bDF1;   // Written by Btl_ExecCommand 0x792210. Read by AtelSys_Btl_224_resi 0x7A8450, AtelSys_Btl_225_resi 0x7A85A0, Btl_RunAiAndCommit 0x7ACEB0 and 2 more
  /* 0xDF2 */ unsigned __int8 m_bDF2;   // Written by sub_7AB360 0x7AB360, Btl_QueueAeonTransition 0x7AE1A0, Btl_ExecCommand 0x792210 and 3 more. Read by Btl_FinishActions 0x7911E0, Btl_StartCommandExec 0x7AE370, sub_7DBEE0 0x7DBEE0
  /* 0xDF3 */ unsigned __int8 m_bDF3;   // Written by Btl_SetupUnitRoster 0x79C110. No reader found
  /* 0xDF4 */ unsigned __int8 m_bDF4;   // Written by Btl_ExecCommand 0x792210, Btl_AeonSpecialCommandHook 0x7B2980. Read by Btl_ExecCommand 0x792210
  /* 0xDF5 */ unsigned __int8 m_bDF5;   // Written by Btl_BindMonsterArchiveSections 0x783B00. Read by BtlEv_StartFirstMatch 0x797D50
  /* 0xDF6 */ unsigned __int8 m_bDF6;   // Written by Btl_BindMonsterArchiveSections 0x783B00. Read by BtlEv_FindSlotRecord 0x798590
  /* 0xDF7 */ unsigned __int8 m_turnEventRan;   // the turn start AI event already ran for this turn (FFX_Btl_SendMenu)
  /* 0xDF8 */ unsigned __int8 m_bDF8;   // Written by Btl_SwapUnitIntoPartySlot 0x7ADAE0, Btl_ApplyPoisonDamage 0x7AFAB0. Read by Btl_TurnQueue_RemoveAt 0x7B20E0
  /* 0xDF9 */ unsigned __int8 m_bDF9;   // AI property 135. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7932B0 0x7932B0, sub_79F380 0x79F380
  /* 0xDFA */ unsigned __int8 m_bDFA;   // No writer found. Read by sub_7DBEE0 0x7DBEE0
  /* 0xDFB */ unsigned __int8 m_bDFB;   // Written by sub_788480 0x788480, Btl_BeginMagicEffect 0x787F20, sub_7AD820 0x7AD820 and 8 more. Read by sub_788480 0x788480, sub_787810 0x787810, sub_7AD820 0x7AD820 and 11 more
  /* 0xDFC */ unsigned __int8 m_bDFC;   // Written by sub_788480 0x788480, Btl_StartCommandExec 0x7AE370, Btl_OnUnitChrReloaded 0x783BC0. Read by sub_788480 0x788480
  /* 0xDFD */ unsigned __int8 m_bDFD;   // Written by Btl_StartMagicPrx 0x788110, Btl_BeginMagicEffect 0x787F20, sub_7AD820 0x7AD820 and 4 more. Read by sub_788480 0x788480, Btl_PumpChrLoadQueue 0x7AD190, sub_792F20 0x792F20 and 1 more
  /* 0xDFE */ unsigned __int8 m_bDFE;   // Written by sub_787AF0 0x787AF0, Btl_BeginMagicEffect 0x787F20, sub_788F30 0x788F30. Read by sub_787AF0 0x787AF0
  /* 0xDFF */ unsigned __int8 m_bDFF;   // Written by sub_7B7D70 0x7B7D70, sub_7B7E20 0x7B7E20. Read by sub_7B7D70 0x7B7D70, sub_7B7E20 0x7B7E20
  /* 0xE00 */ unsigned __int8 m_bE00;   // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788480 0x788480, sub_787D70 0x787D70
  /* 0xE01 */ unsigned __int8 m_bE01;   // Written by Btl_StartCommandExec 0x7AE370. Read by sub_787C30 0x787C30, sub_788F90 0x788F90
  unsigned __int8 _unk_E02[0x2];
  /* 0xE04 */ unsigned __int16 m_wE04;   // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788480 0x788480, sub_787D70 0x787D70
  /* 0xE06 */ unsigned __int16 m_wE06;   // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788F30 0x788F30
  /* 0xE08 */ unsigned __int16 m_wE08;   // Written by Btl_StartCommandExec 0x7AE370. Read by sub_788480 0x788480, sub_787D70 0x787D70
  /* 0xE0A */ unsigned __int16 m_wE0A;   // No writer found. Read by sub_788F30 0x788F30
  /* 0xE0C */ unsigned __int8 m_bE0C;   // AI property 128. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_794E80 0x794E80
  /* 0xE0D */ unsigned __int8 m_bE0D;   // Written by sub_789030 0x789030, Btl_BeginMagicEffect 0x787F20. Read by sub_789030 0x789030, maybe_Btl_GetUnitEffectSlot 0x787CA0, sub_7DBEE0 0x7DBEE0
  /* 0xE0E */ unsigned __int8 m_bE0E;   // AI property 170. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B7680 0x7B7680
  /* 0xE0F */ unsigned __int8 m_bE0F;   // Written by Btl_FinishActions 0x7911E0, Btl_StartCommandExec 0x7AE370. Read by sub_79E8C0 0x79E8C0
  /* 0xE10 */ unsigned __int8 m_bE10;   // AI property 226. Written by sub_79E890 0x79E890, BtlAi_SetProperty 0x7B4B70. Read by sub_79E8C0 0x79E8C0, BtlAi_GetProperty 0x7B2DC0
  /* 0xE11 */ unsigned __int8 m_bE11;   // AI property 227. Written by BtlAi_SetProperty 0x7B4B70. Read by sub_79E8C0 0x79E8C0, BtlAi_GetProperty 0x7B2DC0
  /* 0xE12 */ unsigned __int8 m_bE12;   // Written by AtelSys_Btl_023_resi 0x7A4050, sub_79ECA0 0x79ECA0, AtelSys_Btl_211_resi 0x7A4A90. Read by AtelSys_Btl_023_resi 0x7A4050, AtelSys_Btl_081_poll 0x7A5890, sub_79ECA0 0x79ECA0 and 4 more
  /* 0xE13 */ unsigned __int8 m_bE13;   // Written by sub_79EC40 0x79EC40, sub_79EBA0 0x79EBA0. Read by sub_7DBEE0 0x7DBEE0, AtelSys_Btl_189_poll 0x7A6030, sub_79EC40 0x79EC40 and 4 more
  /* 0xE14 */ unsigned __int8 m_bE14;   // Written by Btl_StartCommandExec 0x7AE370. Read by sub_789030 0x789030, sub_788F90 0x788F90
  /* 0xE15 */ unsigned __int8 m_bE15;   // Written by Btl_SetupUnitRoster 0x79C110. Read by AtelSys_Btl_131_resi 0x7A42A0
  /* 0xE16 */ unsigned __int8 m_bE16;   // Written by Btl_StartCommandExec 0x7AE370, sub_7B7680 0x7B7680. Read by sub_7B7680 0x7B7680
  /* 0xE17 */ unsigned __int8 m_bE17;   // Written by sub_79E3D0 0x79E3D0, Btl_ResetUnitForReentry 0x78D530. Read by AtelSys_Btl_261_poll 0x7A6730
  unsigned __int8 _unk_E18[0x1];
  /* 0xE19 */ unsigned __int8 m_bE19;   // Written by Btl_StartCommandExec 0x7AE370, sub_7B7680 0x7B7680. Read by sub_7B7680 0x7B7680
  /* 0xE1A */ unsigned __int16 m_wE1A;   // AI property 260. Written by Btl_FinishActions 0x7911E0, Btl_StartCommandExec 0x7AE370, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_7B7680 0x7B7680
  /* 0xE1C */ unsigned __int16 m_wE1C;   // Written by sub_788480 0x788480. Read by sub_788480 0x788480
  /* 0xE1E */ unsigned __int16 m_wE1E;   // Written by sub_79EC40 0x79EC40, sub_79ECA0 0x79ECA0, sub_79E590 0x79E590 and 2 more. Read by sub_79E3D0 0x79E3D0, sub_79EC40 0x79EC40, sub_79ECA0 0x79ECA0 and 2 more
  /* 0xE20 */ unsigned __int16 m_wE20;   // Written by sub_79EF40 0x79EF40. Read by sub_79EF40 0x79EF40
  /* 0xE22 */ unsigned __int16 m_wE22;   // Written by sub_79EB10 0x79EB10. Read by sub_79EB10 0x79EB10
  /* 0xE24 */ unsigned __int16 m_wE24;   // Written by sub_79E8C0 0x79E8C0. Read by sub_79E8C0 0x79E8C0
  /* 0xE26 */ unsigned __int16 m_wE26;   // Written by sub_79F160 0x79F160. Read by sub_79F160 0x79F160
  /* 0xE28 */ unsigned __int16 m_wE28;   // Written by sub_79ED40 0x79ED40. Read by sub_79ED40 0x79ED40
  /* 0xE2A */ unsigned __int16 m_wE2A;   // Written by sub_79EBE0 0x79EBE0. Read by sub_79EBE0 0x79EBE0
  /* 0xE2C */ int m_iE2C;   // AI property 87. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by maybe_Btl_SetupUnitChr 0x793670
  /* 0xE30 */ int m_statusFx[24];   // one effect handle per status icon or aura, started by sub_79EF40 0x79EF40 from the status words and
                                    // counters, stopped with sub_7FC450 by FFX_Btl_StopStatusFx 0x79F110
  /* 0xE90 */ int m_statusFx2[4];   // four more effect handles, set by sub_79ED40 0x79ED40, stopped by sub_79EA90 0x79EA90
  /* 0xEA0 */ int m_iEA0;   // Written by sub_79E830 0x79E830, sub_79E8C0 0x79E8C0. Read by sub_79E830 0x79E830, sub_79E8C0 0x79E8C0
  /* 0xEA4 */ int m_iEA4;   // Written by sub_79F160 0x79F160. Read by sub_789030 0x789030, sub_79F160 0x79F160
  /* 0xEA8 */ int m_iEA8;   // Written by AtelSys_Btl_131_resi 0x7A42A0, sub_788E30 0x788E30. Read by sub_788E30 0x788E30, AtelSys_Btl_131_resi 0x7A42A0
  /* 0xEAC */ int m_soundHandles[32];   // handles sub_7A01B0 0x7A01B0 makes with sub_886410 from the unit's data file, released with sub_886D80
                                        // by sub_7A0400 0x7A0400. Btl 108 plays entry n of the m_primaryUnit's list through sub_7A0A30.
                                        // That these are sound banks is [I]
  /* 0xF2C */ unsigned __int8 m_soundReady;   // set by sub_7A0540 0x7A0540 when m_soundLoadsLeft reaches 0, cleared by sub_7A0400. Btl 108 needs it
  /* 0xF2D */ unsigned __int8 m_soundLoadsLeft;   // loads still in flight, counted down by sub_7A0540
  /* 0xF2E */ unsigned __int8 m_soundLoadsIssued;   // loads issued by sub_7A01B0, polled by Btl 124
  unsigned __int8 _unk_F2F[0x5];
  /* 0xF34 */ int m_iF34;   // Written by sub_79F380 0x79F380. Read by sub_795920 0x795920
  /* 0xF38 */ int m_iF38;   // Written by sub_79F380 0x79F380. Read by sub_795920 0x795920
  /* 0xF3C */ int m_iF3C;   // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF40 */ int m_iF40;   // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF44 */ int m_iF44;   // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF48 */ int m_iF48;   // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF4C */ int m_iF4C;   // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF50 */ int m_iF50;   // Written by sub_79F380 0x79F380. Read by sub_79F660 0x79F660
  /* 0xF54 */ int m_iF54;   // Written by sub_795250 0x795250. Read by sub_794FF0 0x794FF0
  /* 0xF58 */ int m_iF58;   // Written by sub_795250 0x795250. Read by sub_794FF0 0x794FF0
  /* 0xF5C */ unsigned __int16 m_odMinigameId;   // overdrive minigame id (BtlOd_StartMinigameForActor 0x7AFD4D)
  /* 0xF5E */ unsigned __int8 m_hitFlags;   // the action flags & 0x7F of the last applied hit (FFX_Btl_ApplyPendingEffects 0x78F0CD)
  /* 0xF5F */ unsigned __int8 m_bF5F;   // Written by Btl_OnUnitDefeated 0x78C740, Btl_ResetUnitForReentry 0x78D530. Read by Btl_OnUnitDefeated 0x78C740
  /* 0xF60 */ int m_overkillHp;   // overkill accumulator, seeded from m_overkillThreshold and reduced by every HP hit
  /* 0xF64 */ unsigned __int8 m_bF64;   // Written by sub_7AD940 0x7AD940. Read by sub_788480 0x788480, AtelSys_Btl_180_resi 0x7A7130
  /* 0xF65 */ unsigned __int8 m_bF65;   // Written by sub_788480 0x788480, AtelSys_Btl_180_resi 0x7A7130. Read by sub_788480 0x788480, AtelSys_Btl_180_resi 0x7A7130
  /* 0xF66 */ unsigned __int16 m_wF66;   // Written by sub_7AD940 0x7AD940. Read by sub_788480 0x788480
  /* 0xF68 */ float m_fF68;   // Written by sub_7B72C0 0x7B72C0. Read by sub_7B72C0 0x7B72C0
  /* 0xF6C */ float m_fF6C;   // Written by sub_79F660 0x79F660. Read by sub_79F660 0x79F660
  /* 0xF70 */ float m_fF70;   // Written by sub_78DDC0 0x78DDC0. Read by sub_794AB0 0x794AB0
  /* 0xF74 */ unsigned __int8 m_bF74;   // AI property 235. Written by Btl_SetupUnitRoster 0x79C110, BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_78F7C0 0x78F7C0
  /* 0xF75 */ unsigned __int8 m_bF75;   // AI property 236. Written by BtlAi_SetProperty 0x7B4B70. Read by BtlAi_GetProperty 0x7B2DC0, sub_78F7C0 0x78F7C0
  /* 0xF76 */ unsigned __int16 m_wF76;   // Written by sub_79E8C0 0x79E8C0. Read by Btl_PlaceArrivedAeon 0x7AC6A0, sub_788F90 0x788F90
  /* 0xF78 */ void *m_atelBlock;   // monster ATEL script block, passed to FFX_BtlEv_AddScriptBlock by Btl_RegisterMonsterMotions 0x7840BB
  /* 0xF7C */ void *m_eventMap;   // event lookup table for FFX_BtlEv_LookupEventEntry (BtlEv_StartFirstMatch 0x797E2F)
  /* 0xF80 */ void *m_params;   // monster archive section 3, the stat and parameter record (FFX_Btl_BindMonsterArchiveSections 0x783B32)
  /* 0xF84 */ void *m_dataSec7;   // monster archive section at header dword [7] (FFX_Btl_BindMonsterArchiveSections 0x783B3D). No reader found
  /* 0xF88 */ void *m_rewards;   // the 280 byte reward record at g_ffxBtlRewardRecords. +6 is the Ronso Rage the unit teaches
  /* 0xF8C */ int m_iF8C;   // Written by Btl_StartMagicPrx 0x788110, Btl_BeginMagicEffect 0x787F20, sub_7AD820 0x7AD820 and 2 more. Read by sub_788480 0x788480, sub_787AF0 0x787AF0, Magic_CallOverlayStep 0x787E00 and 2 more
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
