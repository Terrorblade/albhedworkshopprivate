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
// Addresses are VAs at the preferred base 0x400000. The exe is ASLR'd, see ../README.md.

struct CHR;
struct CHRDATA;
struct CHRPART;
struct CHRANIMF;

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
  /* 0x014 */ float m_alpha;             // FFX_Ch_ModelSetAlpha
  /* 0x018 */ unsigned __int8 m_hidden;  // FFX_Ch_ModelSetHide, bit 1
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
  /* 0x060 */ float m_f60;   // sec9 +64, default +inf (0x7F800000). No reader found
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
  /* 0x194 */ unsigned int m_flags1;   // 0x40 externally driven clip: FFX_Ch_UpdateMotionAll skips the locomotion selector at 0x832E8C AND FFX_Mot_AdvanceFrame skips the whole sequence VM, 0x80 up-vector override, 0x100 no body collision, 0x200 THIS CHR IS THE PLAYER (maintained by FFX_Ch_UpdateRenderJob), 0x400 turn toward m_moveDir, 0x4000 bumped this frame, 0x8000 bumped last frame, 0x10000 blend settled, 0x20000 running, 0x40000 idshade, 0x200000 needs a cloned ClassCharacter, 0x1000000 needs a skin rebuild, 0x2000000 moved, 0x4000000 idle, 0x8000000 moving, 0x20000000 head under water (posY - 8.5 > water level with +Y down, corrected 2026-10-13, CHARACTER_MOTION.md), 0x40000000 height-aware collision, 0x80000000 free move / raw anm clip. Bits 0x07000000 are cleared every frame by FFX_Ch_ClearFlags1StepBits
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
                                                   // factor is 1.0 again, so it only shows during a fade. Set by FFX_Ch_SetAnim360 0x82B5B0 (ATEL 0xA78F6D)
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
  /* 0x834 */ void * m_clonedClassChar;   // cloned ClassCharacter, built only when m_flags1 bit 0x200000 and m_hasClonedClassChar are set. Freed by FFX_Chr_FreeClonedClassCharacter 0x63A160
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