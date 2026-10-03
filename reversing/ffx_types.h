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
  unsigned __int8 _unk_19[0x13];
  /* 0x02C */ void *m_boneBufA;  // 4 bytes per bone, allocated by sub_826BA0 when m_flags1 bit 0x200000 is set
  /* 0x030 */ void *m_boneBufB;
  unsigned __int8 _unk_34[0x4];
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
  unsigned __int8 _unk_18[0x4];
  /* 0x01C */ float m_f1C;
  /* 0x020 */ float m_f20;
  /* 0x024 */ float m_f24;
  /* 0x028 */ float m_f28;
  /* 0x02C */ float m_f2C;
  /* 0x030 */ float m_f30;
  /* 0x034 */ float m_defaultScale;   // FFX_Ch_Allocate seeds the CHR uniform scale from this
  /* 0x038 */ float m_modelScale;   // multiplies attach offsets
  unsigned __int8 _unk_3C[0x30];
  /* 0x06C */ void * m_blobBase;   // compared against a resource base by FFX_Ch_RelocateChrData
  /* 0x070 */ unsigned __int8 m_flags;   // bit 1 block allocated, bit 2 viewer data, bit 4 texture loaded
  /* 0x071 */ char m_name[32];   // e.g. "c001"
  /* 0x091 */ char m_name2[32];   // group/folder name
  unsigned __int8 _unk_B1[0x3];
  /* 0x0B4 */ const char * m_sourceTag;   // "Viewer" for viewer-loaded data
  /* 0x0B8 */ char m_path[64];   // e.g. "/ffx/proj2/chr/prot/<name2>/<name>"
  /* 0x0F8 */ unsigned __int8 m_isFallback;   // 1 when this is the c001 fallback substituted for a missing id
  unsigned __int8 _unk_F9[0x27];
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
// Still unsplit: 0x114 starts a sub-struct of unknown size passed by address to sub_82E3E0.
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
  /* 0x07C */ float m_groundNormalX;   // up-normal of the walkmesh triangle under the CHR. Forced to (0,-1,0) when m_flags1 bit 0x80 ends up clear
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
  unsigned __int8 _unk_C8[0x44];
  /* 0x10C */ int m_i10C;
  /* 0x110 */ float m_f110;   // initialised to 1.0
  unsigned __int8 _unk_114[0x40];
  /* 0x154 */ float m_speed;   // current movement speed, the debug window's "sp". FFX_Ch_AutoLocomotionAnim compares it against m_runThreshold to pick idle/walk/run
  /* 0x158 */ float m_rotY;   // current facing yaw in radians, the debug window's "rot". Slewed toward m_moveDir by the locomotion driver
  unsigned __int8 _unk_15C[0xC];
  /* 0x168 */ float m_moveDir;   // desired movement direction in radians, the debug window's "dir". This plus m_speed is the entire movement input for any CHR
  /* 0x16C */ float m_groundHeight;   // ground height under the CHR, written by FFX_Ch_WalkmeshMove and consumed by the clamp at the end of FFX_Ch_UpdateMotionAll
  /* 0x170 */ float m_runThreshold;   // walk/run speed threshold, defaults to 18.0 when set with a value <= 0
  /* 0x174 */ float m_clipZ;   // the debug CHRInfo window prints this as "ClipZ" (value / 10). It is NOT the collision radius - that is m_collisionRadius at 0x4E8. Its setter scales by 100
  /* 0x178 */ float m_cameLen;   // OUTPUT, not a parameter. FFX_Ch_UpdateCameLenAndZClip 0x82E220
                                //   rewrites it every frame as the distance from m_bodyCentre (0x694,
                                //   not the origin) to the active camera eye, then sets hide bit 0x20
                                //   when it exceeds m_clipZ. Does not control the camera. The debug
                                //   CHRInfo window prints it as "CameLen" (value / 10)
  unsigned __int8 _unk_17C[0x4];
  /* 0x180 */ unsigned __int8 m_hideFlags;   // HIDE mask, 0 means fully visible. Bits from the "Hide   : %x" line: 0x01 Ev, 0x02 Eff, 0x08 Disp, 0x10 Btl, 0x20 Z. Every per-frame pass skips a CHR whose value is non-zero unless m_b181 is set
  /* 0x181 */ unsigned __int8 m_b181;   // process even while hidden - the per-frame passes accept the CHR when this is non-zero
  /* 0x182 */ unsigned __int8 m_groundMode;   // 0 clamp down to m_groundHeight, 1 hard snap, 2 water buoyancy against g_ffxWaterLevel
  /* 0x183 */ unsigned __int8 m_b183;   // 1 for categories 0-3, 0 for 4-6, 3 for 0xF
  /* 0x184 */ unsigned __int8 m_b184;   // field and weapon CHRs get 15
  /* 0x185 */ char m_b185;
  /* 0x186 */ unsigned __int8 m_locomotionMode;   // 0 ground/field -> FFX_Ch_AutoLocomotionAnim, 2 swim -> maybe_FFX_Ch_AutoAnimMode2
  /* 0x187 */ unsigned __int8 m_b187;
  unsigned __int8 _unk_188[0x4];
  /* 0x18C */ int m_partyIndex;   // party/character index, 255 means not a party member
  /* 0x190 */ int m_objId;   // field-object / group id, -1 when unset. A child caches its parent's in m_parentUid
  /* 0x194 */ unsigned int m_flags1;   // 0x40 externally driven clip: FFX_Ch_UpdateMotionAll skips the locomotion selector at 0x832E8C AND FFX_Mot_AdvanceFrame skips the whole sequence VM, 0x80 up-vector override, 0x100 no body collision, 0x200 THIS CHR IS THE PLAYER (maintained by FFX_Ch_UpdateRenderJob), 0x400 turn toward m_moveDir, 0x4000 bumped this frame, 0x8000 bumped last frame, 0x10000 blend settled, 0x20000 running, 0x40000 idshade, 0x200000 needs a cloned ClassCharacter, 0x1000000 needs a skin rebuild, 0x2000000 moved, 0x4000000 idle, 0x8000000 moving, 0x20000000 above water, 0x40000000 height-aware collision, 0x80000000 free move / raw anm clip. Bits 0x07000000 are cleared every frame by FFX_Ch_ClearFlags1StepBits
  /* 0x198 */ unsigned int m_flags2;   // 0x4, 0x8, 0x20 skip the render pass, 0x80, 0x200, 0x400 inside FFX_Ch_Allocate. 0xC00 exempts the CHR from FFX_Ch_MoveChrMemory
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
  /* 0x360 */ struct CHRANIMF m_anim360;
  /* 0x36C */ struct CHRANIMF m_animShadeCoeff;   // pushed to the CharacterShadeCoeff shader constant by FFX_Ch_SetShadeCoeff
  /* 0x378 */ struct CHRANIMF m_anim378;
  unsigned __int8 _unk_384[0x84];
  /* 0x408 */ int m_i408;   // initialised to 1
  unsigned __int8 _unk_40C[0x1];
  /* 0x40D */ unsigned __int8 m_lookAtKind;   // 1 camera, 2 the CHR at m_lookAtTarget, 3 a fixed point
  unsigned __int8 _unk_40E[0xB2];
  /* 0x4C0 */ float m_lookAtTarget[4];   // target CHR pointer or point, read by FFX_Ch_UpdateLookAt
  unsigned __int8 _unk_4D0[0x4];
  /* 0x4D4 */ unsigned __int8 m_blinkState[16];   // idle head-sway and blink state, FFX_Ch_UpdateBlink
  /* 0x4E4 */ float m_collisionRadiusScale;   // negative argument means take CHRDATA.m_f1C
  /* 0x4E8 */ float m_collisionRadius;   // effective radius, recomputed each step as (m_scaleZ + m_scaleX) * 0.5 * m_collisionRadiusScale
  /* 0x4EC */ float m_f4EC;   // negative argument means take CHRDATA.m_f24
  /* 0x4F0 */ float m_f4F0;   // negative argument means take CHRDATA.m_f28
  /* 0x4F4 */ float m_f4F4;   // negative argument means take CHRDATA.m_f2C
  /* 0x4F8 */ float m_f4F8;
  unsigned __int8 _unk_4FC[0x4];
  /* 0x500 */ float m_f500;   // initialised to 1.0
  /* 0x504 */ float m_vertVel;   // vertical velocity, used by the water buoyancy branch of the ground clamp
  unsigned __int8 _unk_508[0x14];
  /* 0x51C */ float m_f51C;
  unsigned __int8 _unk_520[0x4];
  /* 0x524 */ float m_boneWorldPos[88];   // 22 float4 bone world positions, stride 16. The w slot of each is the valid marker. 0x524 + 22*16 == 0x684 exactly
  /* 0x684 */ float m_prevBodyCentre[4];
  /* 0x694 */ float m_bodyCentre[4];
  /* 0x6A4 */ float m_vec6A4[4];   // height extents used by the height-aware collision branch
  /* 0x6B4 */ float m_vec6B4[4];
  unsigned __int8 _unk_6C4[0x18];
  /* 0x6DC */ void * m_p6DC;   // heap block, relocated by FFX_Ch_MoveChrMemory
  unsigned __int8 _unk_6E0[0x1C];
  /* 0x6FC */ int m_i6FC;   // paired with m_i700, both relocated by FFX_Ch_MoveChrMemory
  /* 0x700 */ int m_i700;   // searched by FFX_Ch_FindByI700
  /* 0x704 */ int m_i704;   // relocated by FFX_Ch_MoveChrMemory
  /* 0x708 */ void * m_p708;   // heap block, relocated by FFX_Ch_MoveChrMemory
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
  unsigned __int8 _unk_7EC[0x4];
  /* 0x7F0 */ __int16 m_eventCount;
  /* 0x7F2 */ __int16 m_lastEventFrame;   // the Motev log's frame_lastev
  /* 0x7F4 */ void * m_eventArray;   // stride 8 + 4n. byte0 type (1 SND, 2 EFF, 3 TEX, 5 BTL), byte1 payload dword count, +2 start, +4 end, +6 id
  /* 0x7F8 */ void * m_effCallback;
  /* 0x7FC */ void * m_effContext;
  /* 0x800 */ void * m_anmHeader;   // standalone .anm clip header: [0] total frames, [1] data. Selected by m_flags1 bit 0x80000000
  unsigned __int8 _unk_804[0x10];
  /* 0x814 */ float m_f814;   // looked up from a table keyed on m_id by FFX_Ch_Allocate
  /* 0x818 */ float m_f818;
  unsigned __int8 _unk_81C[0x4];
  /* 0x820 */ unsigned int m_flags3;   // bit 4 is set whenever the character is hidden
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
  unsigned __int8 _unk_840[0x30];
  /* 0x870 */ unsigned __int8 m_jointComposerMode;   // initialised to 1
  /* 0x871 */ unsigned __int8 m_forceSmoothComposer;
  unsigned __int8 _unk_872[0x2];
  /* 0x874 */ void * m_pMeshPtrs;   // per-mesh pointer array, freed by FFX_Ch_Dispose
  /* 0x878 */ int m_updateStage;   // worker-thread stage marker: 1 queued, then 2, 3, 0 done
  /* 0x87C */ unsigned __int8 m_b87C;
  unsigned __int8 _unk_87D[0x3];
};