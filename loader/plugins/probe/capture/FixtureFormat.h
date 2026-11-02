#pragma once

#include <windows.h>
#include <stddef.h>

// The probe fixture format, version 1. THE OTHER HALF OF THIS FILE IS PYTHON:
// projectzanarkand/tools/probe_fixture.py parses exactly these layouts, and
// reversing/PROBE_FIXTURES.md documents them field by field. Change all three together,
// and bump FormatVersion for any change a reader would misparse.
//
// One file per recipe per capture session. A file is a 64 byte FileHeader, a kind
// specific extension of FileHeader::extensionBytes, then fixed size records from
// FileHeader::headerBytes onwards, all little endian. There is no record count. A reader
// takes (file size - headerBytes) / recordBytes and ignores a partial tail, which is what
// a capture killed mid write leaves.
//
// Floats are stored as the game held them, bit for bit, so a NaN or a -0 survives.
// Pointers are stored as the raw 32 bit value from this run, which is only meaningful
// next to FileHeader::exeModuleBase and only as "null or not" across runs.
//
// Everything is packed to 1 so the layout is the declaration and nothing else. The
// static_asserts at the end pin every size and the offsets the Python side relies on.

namespace probe
{

#pragma pack(push, 1)

	const char FormatMagic[8] = { 'A', 'B', 'W', 'P', 'R', 'O', 'B', 'E' };
	const WORD FormatVersion = 1;

	enum FixtureKind
	{
		KindPacing = 1, // FFX_StepPacing, one record per call
		KindCamera = 2, // FFX_Came_ActivateScreen, one record per call
		KindMotion = 3, // FFX_Ch_UpdateMotionAll, one record per in use CHR per side
		KindJoints = 4, // FFX_Mot_AdvanceFrame, one record per chosen CHR per side

		// The battle recipe writes three files, because its three record shapes differ by
		// two orders of magnitude and a file holds one record size.
		KindBattle = 5,      // one snapshot per step while a battle runs
		KindBattleEvent = 6, // one per AI run, queue add and hit, from four hooks
		KindBattleDump = 7,  // whole unit records and the save block at six moments of a battle
	};
	const int KindCount = 8; // kinds are 1..KindCount-1, for arrays indexed by kind

	struct FileHeader
	{
		char magic[8];
		WORD version;
		WORD kind;
		DWORD headerBytes; // where the first record starts, this header plus the extension
		DWORD recordBytes;
		DWORD exeTimeDateStamp; // FFX.exe's PE header, the build check
		DWORD exeSizeOfImage;
		DWORD exeCheckSum;
		DWORD exeModuleBase;              // where FFX.exe was loaded this run
		DWORD sessionIndex;               // 0 for the first capture of this process, then 1, 2 ...
		unsigned long long startFileTime; // UTC FILETIME when the capture started
		DWORD extensionBytes;
		DWORD flags; // none defined yet, 0
		DWORD reserved[2];
	};

	// Every record starts with this.
	//
	// pacingOrdinal is the probe's own count of FFX_StepPacing calls since the plugin
	// loaded, and is the key that ties the files together: a camera, motion or joints
	// record belongs to the simulation step whose pacing record has the same ordinal.
	// StepPacing does not run while the in-game menu holds MenuSysRunningAlt, so steps
	// taken there share the ordinal of the last step that paced, and callIndex keeps
	// counting.
	struct RecordPrefix
	{
		DWORD seq;             // this record's index in its file, from 0
		DWORD phyreFrameTick;  // g_phyreFrameTickCount, the presented frame
		DWORD mainStepCounter; // g_ffxMainStepCounter
		DWORD pacingOrdinal;
		WORD callIndex; // pacing: call within the presented frame, camera and motion: call
		                // since the last pacing call, so motion's is the sub step index.
		                // Joints: the motion call it ran inside
		BYTE phase;     // motion and joints: 0 before the call, 1 after. Others: 0
		BYTE flags;     // per kind, see each record
	};

	// ---------------------------------------------------------------------------
	// Pacing
	// ---------------------------------------------------------------------------

	struct PacingVarsRecord
	{
		int pendingSteps;
		int catchUpExtraSteps;
		int pacingMode;
		float frac60;
		float frac30;
		DWORD clock60Hz;
		int menuSysRunning;
		int menuSysRunningAlt;
		int menuOpenPending;
		int saveUiState;
	};

	struct TimingTrackRecord
	{
		float nextTime;
		float tolerance;
		float dueTime;
		float elapsed;
		long long startUs;
		long long pauseUsPending;
		long long pauseUsTotal;
		int firstStep;
		DWORD pad;
	};

	// prefix.flags: bit 0 the frame inputs were read, bit 1 the pacing globals, bit 2 the
	// timing track globals. A clear bit means that block is zero, not that it was zero.
	const BYTE PacingFlagFrame = 0x01;
	const BYTE PacingFlagVars = 0x02;
	const BYTE PacingFlagTrack = 0x04;

	// allowCatchUp: 1 or 0 is FFX_StepPacing_AllowCatchUp's answer, asked by the probe on
	// entry. It is only asked when pendingSteps is 0 on entry, the only time the engine can
	// consult it, and 0xFF means it was not asked or could not be called.
	const BYTE AllowCatchUpNotAsked = 0xFF;

	struct PacingRecord
	{
		RecordPrefix prefix;
		float dt;            // the argument, booster scaled by FFX_MainStep
		float frameDelta;    // application + 0x24
		float fixedTimeStep; // application + 0x3C
		DWORD appFrameNumber;
		int frameRateDivider;
		int boosterEnabled;
		int boosterSpeedIndex;
		int subStepCount;
		BYTE allowCatchUp;
		BYTE pad[3];
		PacingVarsRecord entry;
		PacingVarsRecord exit;
		TimingTrackRecord trackEntry;
		TimingTrackRecord trackExit;
	};

	// ---------------------------------------------------------------------------
	// Camera
	// ---------------------------------------------------------------------------

	const int CameScreenBytes = 448;
	const int CameScreenCount = 3;
	const int CameSlotBytes = 0xDA0;
	const int CameSlotCount = 3;

	struct CameraExtension
	{
		DWORD screenBytes;
		DWORD screenCount;
		DWORD slotBytes;
		DWORD slotCount;
	};

	// prefix.flags: bit 0 the globals were read, bit 1 the stream 9 state, bit 2 the
	// matrices, bit 3 the screens and slots.
	const BYTE CameraFlagGlobals = 0x01;
	const BYTE CameraFlagRng = 0x02;
	const BYTE CameraFlagMatrices = 0x04;
	const BYTE CameraFlagBlocks = 0x08;

	struct CameraRecord
	{
		RecordPrefix prefix;
		int screen; // the argument
		int result; // 1 past the last screen, -1 disabled, 0 built
		DWORD rngStream9Entry;
		DWORD rngStream9Exit;
		int activeScreenEntry; // index, -1 for none
		int activeSlotEntry;
		int activeScreenExit;
		int activeSlotExit;
		DWORD directLookAtFlag;
		DWORD overrideFlag;
		DWORD levelNameHash;
		int pendingGlobalEffect;
		int pendingBattleEffect;
		int ps2FrameWidth;
		int ps2FrameHeight;
		int magicId;
		int magicPrevId;
		DWORD magicFreezeFlag;
		int liveSceneId;
		BYTE magicFrozen; // MagicFile__isCameraFrozen on entry, 0xFF not asked
		BYTE nearLatchEntry;
		BYTE nearLatchExit;
		BYTE levelNameHashValid;
		BYTE directBlock[64]; // 0x1136FB8, bias x y, fov mode, roll, eye, at, up
		float viewEntry[16];
		float projEntry[16];
		float viewExit[16];
		float projExit[16];
		BYTE screensEntry[CameScreenBytes * CameScreenCount];
		BYTE slotsEntry[CameSlotBytes * CameSlotCount];
		BYTE screensExit[CameScreenBytes * CameScreenCount];
	};

	// ---------------------------------------------------------------------------
	// Motion
	// ---------------------------------------------------------------------------

	// The CHR bytes a motion record carries, as (offset, length) pairs in CHR order. The
	// same table goes into the file after MotionExtension, so a reader never hardcodes
	// it. It is CHARACTER_MOTION.md section 10.4's field list, widened to whole dwords and,
	// for +0x698 and +0x6A8, to the two vectors they sit in.
	struct ChrSpan
	{
		WORD offset;
		WORD bytes;
	};

	const ChrSpan MotionChrSpans[] = {
		{ 0x000, 4 },   // m_id, m_inUse, +0x003
		{ 0x00C, 32 },  // pos, prevPos
		{ 0x03C, 28 },  // walkmeshPos, vel
		{ 0x05C, 4 },   // m_scaleX
		{ 0x064, 4 },   // m_scaleZ
		{ 0x07C, 16 },  // ground normal and the w the move writes
		{ 0x154, 8 },   // m_speed, m_rotY
		{ 0x168, 12 },  // m_moveDir, m_groundHeight, m_runThreshold
		{ 0x180, 4 },   // m_hideFlags, m_b181, m_groundMode, +0x183
		{ 0x184, 4 },   // +0x184..0x187, m_locomotionMode at +0x186
		{ 0x194, 8 },   // m_flags1, m_flags2
		{ 0x330, 84 },  // the seven CHRANIMF
		{ 0x4E4, 64 },  // collision radius, height band, vertVel and their neighbours
		{ 0x694, 32 },  // m_bodyCentre, m_vec6A4, so +0x698 and +0x6A8
		{ 0x714, 24 },  // m_seqPC (raw), clip start, end, span, loop counts, playing, wait
		{ 0x734, 4 },   // +0x734..0x737, m_motionRequestActive at +0x736
		{ 0x740, 4 },   // m_frame
		{ 0x750, 8 },   // speedMulA, hokan countdown, playSpeed, frameDelta
		{ 0x75C, 128 }, // m_slots[32]
		{ 0x7E0, 4 },   // m_frameRateScale
		{ 0x800, 4 },   // m_anmHeader (raw)
		{ 0x824, 8 },   // m_walkmeshTri, +0x826, m_groundAttrs
	};
	const int MotionChrSpanCount = (int)(sizeof(MotionChrSpans) / sizeof(MotionChrSpans[0]));
	const int MotionChrBytes = 488; // the sum of the lengths, checked at startup
	const DWORD ChrStride = 0x880;

	struct MotionExtension
	{
		DWORD chrStride;
		DWORD spanCount;
		ChrSpan spans[sizeof(MotionChrSpans) / sizeof(MotionChrSpans[0])];
	};

	// prefix.flags: bit 0 this record has no CHR (none were in use, so it carries only the
	// globals), bit 1 the globals were read, bit 2 the pool span validated.
	const BYTE MotionFlagNoChr = 0x01;
	const BYTE MotionFlagGlobals = 0x02;
	const BYTE MotionFlagPool = 0x04;

	// chrFlags.
	const WORD ChrFlagControlled = 0x0001; // this slot is g_ffxControlledChr
	const WORD ChrFlagSeqPc = 0x0002;      // m_seqPC non-null
	const WORD ChrFlagAnmHeader = 0x0004;  // m_anmHeader non-null
	const WORD ChrFlagAnmCount = 0x0008;   // anmFrameCount was read from [m_anmHeader]
	const WORD ChrFlagFaulted = 0x0010;    // the copy faulted, the bytes are partial

	const WORD NoSlot = 0xFFFF;

	struct MotionRecord
	{
		RecordPrefix prefix;
		float dt; // always 0x3D08B27A in retail, a check
		DWORD chrArray;
		int chrCount;
		DWORD controlledChr;
		float walkmeshScale;
		int walkmeshTriCount;
		float waterLevel;
		float waterLevelMax;
		int groundRemap[16]; // codes 48..63
		int debugNoClip;
		DWORD stepScale88;
		int motHalfSpeed;
		BYTE chrUpdateFrozen;
		BYTE motKeepSpeed;
		BYTE motionUpdateEnabled;
		BYTE pad0;
		int currentEventId;
		int liveSceneId;
		char currentEventName[16];
		WORD slot;       // pool index, NoSlot when prefix.flags has MotionFlagNoChr
		WORD inUseCount; // CHRs in use on this side of this call
		WORD ordinal;    // this CHR's position among them
		WORD chrFlags;
		short anmFrameCount; // the s16 at [m_anmHeader], 0 unless ChrFlagAnmCount
		WORD pad1;
		BYTE chr[MotionChrBytes];
	};

	// ---------------------------------------------------------------------------
	// Joints, CHARACTER_MOTION.md section 11.12, for PROGRESS 10.4
	// ---------------------------------------------------------------------------

	// The CHR bytes a joints record carries, the same (offset, length) shape as the motion
	// spans and also written into the file. 0x000..0x007 is m_id and the model name
	// pointer, 0x194..0x19F m_flags1, +0x198 and the parent, 0x1C0..0x1C7 the skeleton
	// and CHRDATA pointers, 0x328 the joint array, 0x684..0x6A3 the two body centres,
	// 0x70C..0x7FF the whole playback block, 0x800 m_anmHeader, 0x820 m_flags3, and the
	// two composer bytes 0x870 and 0x871.
	const ChrSpan JointsChrSpans[] = {
		{ 0x000, 8 },
		{ 0x194, 12 },
		{ 0x1C0, 8 },
		{ 0x328, 4 },
		{ 0x684, 32 },
		{ 0x70C, 244 },
		{ 0x800, 4 },
		{ 0x820, 4 },
		{ 0x870, 2 },
	};
	const int JointsChrSpanCount = (int)(sizeof(JointsChrSpans) / sizeof(JointsChrSpans[0]));
	const int JointsChrBytes = 318; // the sum of the lengths, checked at startup

	// The playback block, 0x70C..0x7FF, copied again when FFX_Mot_StepSequence returns.
	const WORD PlaybackBlockOffset = 0x70C;
	const int PlaybackBlockBytes = 244;

	// Per joint: the composed dwords 57..79 (+0xE4..+0x13F) then byte 324 (+0x144), the
	// Auto composer's switch, then padding.
	const int JointComposedBytes = 92;
	const int JointEntryBytes = 96;
	const int ChannelEntryBytes = 44;
	const int ChannelsPerJoint = 9;
	const int ChannelBlockBytes = ChannelEntryBytes * ChannelsPerJoint; // 396 a joint

	// The default for probe.joints_max, c001's skeleton. A CHR with more joints than
	// the capacity is recorded up to it and flagged JointsChrTruncated.
	const int DefaultJointCapacity = 136;

	struct JointsExtension
	{
		DWORD chrStride;         // 0x880
		DWORD jointCapacity;     // joints each record has room for
		DWORD channelEntryBytes; // 44
		DWORD channelsPerJoint;  // 9
		DWORD jointSourceStride; // 352, the engine's joint size
		DWORD jointDwordFirst;   // 57
		DWORD jointDwordCount;   // 23
		DWORD jointEntryBytes;   // 96, the record's joint size
		DWORD headBytes;         // sizeof(JointsRecordHead), where the channels start
		DWORD spanCount;
		ChrSpan spans[sizeof(JointsChrSpans) / sizeof(JointsChrSpans[0])];
	};

	// prefix.flags. A clear bit means that block is zero.
	const BYTE JointsFlagGlobals = 0x01;  // the globals were read
	const BYTE JointsFlagCamera = 0x02;   // the camera slot fields were read
	const BYTE JointsFlagChannels = 0x04; // the channel array was copied
	const BYTE JointsFlagJoints = 0x08;   // the joint array was copied
	const BYTE JointsFlagSeqExit = 0x10;  // seqExit holds the block after StepSequence
	const BYTE JointsFlagRebased = 0x20;  // bundle pointers are offsets, see below

	// chrFlags.
	const WORD JointsChrControlled = 0x0001; // this CHR is g_ffxControlledChr
	const WORD JointsChrTruncated = 0x0002;  // more skeleton joints than the capacity
	const WORD JointsChrNoSkeleton = 0x0004; // no skeleton, so no joint count
	const WORD JointsChrNoChannels = 0x0008; // m_channels is null
	const WORD JointsChrNoJoints = 0x0010;   // the joint array pointer is null
	const WORD JointsChrNoBundle = 0x0020;   // m_motionBundle is null, pointers left raw
	const WORD JointsChrLinkRaw = 0x0040;    // a channel link did not land on a channel
	const WORD JointsChrFaulted = 0x0080;    // a copy faulted, the bytes are partial

	// The two composer queries. NotCalled when the engine did not ask, which is the case
	// whenever 0x871 is set, 0x870 is clear or m_flags3 bit 2 is set, and for NearCamera
	// also whenever Stationary said false. Phase 0 records always carry NotCalled.
	const BYTE ComposerNotCalled = 0xFF;

	// POINTER REBASING, done by the probe when JointsFlagRebased is set:
	//   channel +0x08 and +0x0C, CHR 0x70C, 0x710, 0x714 and 0x7F4, in the CHR span and in
	//   seqExit: non-null -> pointer - bundleBase (u32 wraparound, so it is lossless).
	//   channel +0x00 and CHR 0x74C: null -> 0xFFFFFFFF, a channel -> its index,
	//   (pointer - channelsBase) / 44, anything else left raw and JointsChrLinkRaw set.
	// Null stays 0 for the bundle pointers. bundleBase, channelsBase and jointsBase stay
	// raw in the head, and so do CHR 0x748 and 0x7E8 in the spans.

	struct JointsRecordHead
	{
		RecordPrefix prefix;
		DWORD chrPointer;    // the CHR, raw
		int applyBlend;      // the second argument
		DWORD bundleBase;    // [CHR+0x7E8], raw
		DWORD channelsBase;  // [CHR+0x748], raw
		DWORD jointsBase;    // [CHR+0x328], raw
		WORD skeletonJoints; // u16 at [CHR+0x1C0]+0x0A
		WORD jointsCopied;   // min(skeletonJoints, capacity)
		WORD chrFlags;
		BYTE stationary; // FFX_Ch_IsNearlyStationary's answer, or ComposerNotCalled
		BYTE nearCamera; // FFX_Ch_IsNearCameraForSmoothing's answer, or ComposerNotCalled
		BYTE stationaryCalls;
		BYTE nearCameraCalls;
		BYTE stepSequenceCalls;
		BYTE atelContextByte; // byte 0 of the selected ATEL context
		int liveSceneId;
		WORD storyProgress;
		BYTE btlSubPhase;
		BYTE motKeepSpeed;
		int currentEventId;
		DWORD stepScale88;
		int motHalfSpeed;
		DWORD camSlot;           // g_ffxCameActiveSlot, raw
		int camSlotIndex;        // -1 when it is not one of the three
		float camEye[4];         // slot +0x65C
		float camPitchWhenSet;   // slot +0x6B0
		float camPitchWhenClear; // slot +0x6D0
		BYTE camPitchSelect;     // slot +0x64D
		BYTE atelContextValid;
		WORD slot;          // index in the CHR pool, NoSlot when it is not in the pool
		float chrDataScale; // [[CHR+0x1C4]+0x38]
		char modelName[8];  // the first 8 bytes at [CHR+4], NUL padded
		BYTE chr[JointsChrBytes];
		WORD pad1;
		int stepSequenceResult; // what StepSequence returned, 0 when it did not run
		BYTE seqExit[PlaybackBlockBytes];
	};
	// Followed by BYTE channels[capacity][396] and then BYTE joints[capacity][96]. Only the
	// first jointsCopied of each are filled, the rest are zero.

	inline DWORD JointsRecordBytes(DWORD capacity)
	{
		return (DWORD)sizeof(JointsRecordHead) + capacity * (DWORD)(ChannelBlockBytes + JointEntryBytes);
	}

	// ---------------------------------------------------------------------------
	// Battle, kinds 5, 6 and 7, for Project Zanarkand PROGRESS 13
	// ---------------------------------------------------------------------------
	//
	// reversing/PROBE_FIXTURES.md section 10. Three files from one recipe:
	//
	//   5 battle       one BattleStepRecord per FFX_StepPacing call while a battle runs, plus
	//                  one on the first step after it, flagged BattleStepEnded
	//   6 battleevent  one BattleEventRecord per call of FFX_Btl_RunAiAndCommit,
	//                  FFX_Btl_CmdQueue_Push, FFX_Btl_CmdQueue_Insert and
	//                  FFX_Btl_ComputeEffectForTarget, written when the call returns
	//   7 battledump   one BattleDump at each BattleDumpReason
	//
	// A STEP SNAPSHOT IS THE STATE AT THE START OF THE STEP. It is taken on FFX_StepPacing's
	// entry, which FFX_MainStep reaches before its sub step loop and so before
	// FFX_Btl_MainStep, so record N holds what the battle looked like after the previous
	// step. The event records of step N (the same pacingOrdinal) happened between record N
	// and record N + 1.
	//
	// Every file of the three numbers battles the same way: battleOrdinal counts the battles
	// this capture session has seen, from 0, and battleStep counts this battle's steps from
	// the first step it was seen running, from 0.

	// The unit record bytes a step record carries per unit, as (offset, length) pairs in
	// record order, also written into the battle file's extension. Picked from Project
	// Zanarkand's src/ffx/btl/BattleUnit.h, the fields that move during a battle. The whole
	// 3984 byte record is in the dumps.
	const ChrSpan BattleUnitSpans[] = {
		{ 0x004, 16 },  // chr id, unit index, type id, exists, owns data file, primary unit
		{ 0x590, 72 },  // rom flags .. max HP, max MP, the eight stats, overdrive mode, gauge
		                // and max, HP at +0x5D0, MP at +0x5D4
		{ 0x604, 100 }, // status 1 and 2 with their counters, saved, permanent, auto and SOS
		                // statuses, derived flags, resists, CTB +0x65C, CTB base, Cheer..Jinx
		{ 0x6C8, 40 },  // escape command, costs, regen, summoner, cover, aeon recovery,
		                // counters, work HP, MP and CTB +0x6E4..+0x6EF
		{ 0x700, 24 },  // defeat style and cause, bribe, gil steal, turn deltas
		{ 0xD24, 28 },  // overdrive minigame state and the damage sums
		{ 0xDC0, 64 },  // target masks, on field, KO +0xDCC, escaped +0xDCE, ready +0xDD6,
		                // queue indices, action state +0xDF0, AI controlled +0xDF3
		{ 0xF5C, 12 },  // executing command id, last hit flags, overkill
	};
	const int BattleUnitSpanCount = (int)(sizeof(BattleUnitSpans) / sizeof(BattleUnitSpans[0]));
	const int BattleUnitPackedBytes = 356; // the sum of the lengths, checked at startup
	const DWORD BattleUnitStride = 3984;
	const int BattleUnitCount = 31;

	// Module globals a step record carries, packed in this order into globals[], as RVAs (IDA
	// VA minus 0x400000) and lengths. Also in the extension.
	struct GlobalSpan
	{
		DWORD rva;
		DWORD bytes;
	};
	const GlobalSpan BattleGlobalSpans[] = {
		{ 0x00D2A8E0, 0x50 }, // 0x112A8E0 g_ffxBtlSubPhase, CTB pause, pending kind, end result,
		                      // script hold, the debug flag block 0x112A8F8..0x112A927
		{ 0x00D2BDE0, 0x10 }, // 0x112BDE0 turn and command queue counts, CTB sub tick and limit
		{ 0x00D2BE90, 4 },    // 0x112BE90 g_ffxBtlPendingGilCost
		{ 0x00D2C9D0, 0xA0 }, // 0x112C9D0 battle flow: advantage, acting unit, end wait frames,
		                      // forced result, game over, escape, script mode, start hold
		{ 0x00D333C0, 0x24 }, // 0x11333C0 CTB ready count and the 31 byte ready list
		{ 0x00D36A68, 0x58 }, // 0x1136A68 AI staging: script context, no reorder, priority, and
		                      // the 72 byte staged command at 0x1136A78
		{ 0x01FCC088, 8 },    // 0x23CC088 the menu owner unit and the menu open mask
	};
	const int BattleGlobalSpanCount = (int)(sizeof(BattleGlobalSpans) / sizeof(BattleGlobalSpans[0]));
	const int BattleGlobalBytes = 392; // the sum of the lengths, checked at startup

	const int BattleStreamCount = 68;
	const int BattleTurnQueueSlots = 62;
	const int BattleTurnEntryBytes = 8;
	const int BattleCommandBytes = 72;
	const int BattleCmdQueueSlotsRecorded = 16; // of 62, the rest is flagged, never seen used

	struct BattleExtension
	{
		DWORD unitStride;      // 3984
		DWORD unitCount;       // 31
		DWORD unitPackedBytes; // 356
		DWORD streamCount;     // 68
		DWORD turnQueueSlots;  // 62
		DWORD cmdQueueSlots;   // 16 recorded per step
		DWORD globalBytes;     // 392
		DWORD unitSpanCount;   // then that many ChrSpan
		ChrSpan unitSpans[sizeof(BattleUnitSpans) / sizeof(BattleUnitSpans[0])];
		DWORD globalSpanCount; // then that many GlobalSpan
		GlobalSpan globalSpans[sizeof(BattleGlobalSpans) / sizeof(BattleGlobalSpans[0])];
	};

	// prefix.flags, kind 5. A clear bit means that block is zero.
	const BYTE BattleStepGlobals = 0x01;      // globals[] and the queues were copied
	const BYTE BattleStepRng = 0x02;          // streams and the two LCGs
	const BYTE BattleStepUnits = 0x04;        // units[] was copied
	const BYTE BattleStepCmdTruncated = 0x08; // the command queue held more than 16
	const BYTE BattleStepFaulted = 0x10;      // a copy faulted, the bytes are partial
	const BYTE BattleStepBegan = 0x20;        // the first step this battle was seen
	const BYTE BattleStepEnded = 0x40;        // the first step after it, units[] is zero

	struct BattleStepRecord
	{
		RecordPrefix prefix;
		DWORD battleOrdinal;
		DWORD battleStep;
		DWORD unitsBase;       // the unit array, raw
		DWORD sideRecordsBase; // the side record array, raw
		BYTE subPhase;         // g_ffxBtlSubPhase, 0 only on the ended record
		BYTE turnQueueCount;
		BYTE cmdQueueCount;
		BYTE cmdQueueCopied; // min(cmdQueueCount, 16)
		DWORD battleRand;    // g_ffxBattleRandState, FFX_Btl_Rand's LCG
		DWORD effectRand;
		DWORD streams[BattleStreamCount]; // FFX_Rand_Stream's 68 states
		BYTE turnQueue[BattleTurnQueueSlots * BattleTurnEntryBytes];
		BYTE cmdQueue[BattleCmdQueueSlotsRecorded * BattleCommandBytes];
		BYTE globals[BattleGlobalBytes];
		BYTE units[BattleUnitCount * BattleUnitPackedBytes]; // unit i at i * 356
	};

	// Kind 6. The four hooks, one record per call, written when the call returns, holding
	// the state on entry and on exit. They nest (an AI run commits, which pushes, and a
	// script kill runs the defeat AI inside the outer run), so a record written later can
	// have an earlier callId: callId is the entry order, seq the exit order.
	enum BattleEventType
	{
		BattleEventAiRun = 1,       // FFX_Btl_RunAiAndCommit
		BattleEventQueuePush = 2,   // FFX_Btl_CmdQueue_Push
		BattleEventQueueInsert = 3, // FFX_Btl_CmdQueue_Insert
		BattleEventHit = 4,         // FFX_Btl_ComputeEffectForTarget
	};

	const int BattleEventArgCount = 12;
	const int BattleClass3Capacity = 1024; // 8 monsters of the largest shipped AI, 128 bytes
	const BYTE NoQueueSlot = 0xFF;
	const DWORD NoParentCall = 0xFFFFFFFF;
	const DWORD NoClass3Group = 0xFFFFFFFF;

	// prefix.flags, kind 6.
	const BYTE BattleEventClass3 = 0x01;          // class3[] was copied
	const BYTE BattleEventClass3Truncated = 0x02; // the storage is larger than 1024
	const BYTE BattleEventFaulted = 0x04;         // a copy through an engine pointer faulted
	const BYTE BattleEventRng = 0x08;             // the stream and LCG fields were read
	const BYTE BattleEventSlotFound = 0x10;       // queue: slot says where the record landed

	struct BattleEventRecord
	{
		RecordPrefix prefix; // callIndex is the depth, phase 0
		WORD type;           // BattleEventType
		WORD depth;          // 0 for a call not inside another of the four
		DWORD callId;        // entry order in the session, from 0
		DWORD parentCallId;  // the enclosing call, NoParentCall at depth 0
		DWORD battleOrdinal;
		DWORD battleStep;
		DWORD args[BattleEventArgCount]; // the call's arguments as raw dwords, the rest 0
		int result;                      // what the call returned
		BYTE subPhaseEntry;
		BYTE subPhaseExit;
		BYTE cmdQueueCountEntry;
		BYTE cmdQueueCountExit;
		BYTE turnQueueCountEntry;
		BYTE turnQueueCountExit;
		BYTE slot; // queue: the slot the record landed in, NoQueueSlot when not found
		BYTE pad0;
		int pendingGil; // g_ffxBtlPendingGilCost on entry
		DWORD battleRandEntry;
		DWORD battleRandExit;
		DWORD streamsEntry[BattleStreamCount];
		DWORD streamsExit[BattleStreamCount];
		// AI run: blockEntry is the fill record args[5] points at, on entry (zero when it is
		// null), blockExit is g_ffxBtlAiStagedCmd on exit, the record the script staged.
		// Queue: blockEntry is the 72 bytes args[1] points at, on entry, blockExit the queue
		// slot it landed in, on exit. Hit: the 44 byte hit record args[6] points at, on entry
		// and on exit, the rest zero.
		BYTE blockEntry[BattleCommandBytes];
		BYTE blockExit[BattleCommandBytes];
		int aiPriority;                    // AI run: g_ffxBtlAiStagedPriority on exit
		int aiNoReorder;                   // AI run: g_ffxBtlAiStagedNoReorder on exit
		int hitExtra[4];                   // hit: the three drain dwords args[7] points at and *args[10], on exit
		DWORD class3Group;                 // AI run: 0 for ATEL context 2, 1 for context 3, NoClass3Group
		DWORD class3Bytes;                 // the storage's whole size
		DWORD class3Base;                  // raw
		BYTE class3[BattleClass3Capacity]; // AI run: the private class 3 storage on exit
	};

	// Kind 7. Why a dump was taken.
	enum BattleDumpReason
	{
		BattleDumpSeen = 1,          // the first step the battle was seen, before it loaded
		BattleDumpSubPhase1 = 2,     // the first step at sub phase 1, the battle running
		BattleDumpFirstMenu = 3,     // the first step the command menu was open, 13.1's prompt
		BattleDumpCommitted = 4,     // FFX_Btl_CommitActorsToSave just returned, units still live
		BattleDumpGone = 5,          // the first step after the battle, no units
		BattleDumpResultsClosed = 6, // the first quiet step after the results screen ran
	};

	// sections, which parts of the dump hold data.
	const DWORD BattleDumpUnits = 0x01;
	const DWORD BattleDumpSides = 0x02;
	const DWORD BattleDumpSave = 0x04;
	const DWORD BattleDumpWork = 0x08;
	const DWORD BattleDumpAi = 0x10;
	const DWORD BattleDumpClass3Party = 0x20;   // group 0, ATEL context 2
	const DWORD BattleDumpClass3Monster = 0x40; // group 1, ATEL context 3
	const DWORD BattleDumpRng = 0x80;
	const DWORD BattleDumpFaulted = 0x100;

	const int BattleSideRecordCount = 62;
	const DWORD BattleSideRecordStride = 912;
	const int SaveBlockBytes = 0x68C0;    // 26816, g_ffxSaveData 0x112CA90
	const int BattleWorkBytes = 0x21B0;   // 0x112A8E0 up to the save block
	const int BattleAiGlobalBytes = 0x58; // 0x1136A68, as in the step globals

	struct BattleDumpHead
	{
		RecordPrefix prefix; // callIndex is the reason
		DWORD reason;
		DWORD battleOrdinal;
		DWORD battleStep;
		DWORD sections;
		DWORD unitsBase;
		DWORD sideRecordsBase;
		DWORD class3Base[2];
		DWORD class3Bytes[2]; // whole sizes, the copies hold at most 1024 each
		BYTE subPhase;
		BYTE pad[3];
		DWORD battleRand;
		DWORD effectRand;
		DWORD streams[BattleStreamCount];
	};

	struct BattleDump
	{
		BattleDumpHead head;
		BYTE units[BattleUnitCount * BattleUnitStride];
		BYTE sides[BattleSideRecordCount * BattleSideRecordStride];
		BYTE save[SaveBlockBytes];
		BYTE work[BattleWorkBytes];
		BYTE ai[BattleAiGlobalBytes];
		BYTE class3[2][BattleClass3Capacity];
	};

	struct BattleDumpExtension
	{
		DWORD headBytes;      // sizeof(BattleDumpHead)
		DWORD unitStride;     // 3984
		DWORD unitCount;      // 31
		DWORD sideStride;     // 912
		DWORD sideCount;      // 62
		DWORD saveBytes;      // 26816
		DWORD workRva;        // 0x00D2A8E0
		DWORD workBytes;      // 0x21B0
		DWORD aiRva;          // 0x00D36A68
		DWORD aiBytes;        // 0x58
		DWORD class3Capacity; // 1024
	};

	struct BattleEventExtension
	{
		DWORD argCount;       // 12
		DWORD streamCount;    // 68
		DWORD blockBytes;     // 72
		DWORD class3Capacity; // 1024
	};

#pragma pack(pop)

	static_assert(sizeof(FileHeader) == 64, "FileHeader is 64 bytes");
	static_assert(sizeof(RecordPrefix) == 20, "RecordPrefix is 20 bytes");
	static_assert(sizeof(PacingVarsRecord) == 40, "PacingVarsRecord is 40 bytes");
	static_assert(sizeof(TimingTrackRecord) == 48, "TimingTrackRecord is 48 bytes");
	static_assert(sizeof(PacingRecord) == 232, "PacingRecord is 232 bytes");
	static_assert(offsetof(PacingRecord, entry) == 56, "pacing entry block at 56");
	static_assert(offsetof(PacingRecord, trackEntry) == 136, "pacing track block at 136");
	static_assert(sizeof(CameraExtension) == 16, "CameraExtension is 16 bytes");
	static_assert(offsetof(CameraRecord, directBlock) == 100, "camera direct block at 100");
	static_assert(offsetof(CameraRecord, viewEntry) == 164, "camera matrices at 164");
	static_assert(offsetof(CameraRecord, screensEntry) == 420, "camera screens at 420");
	static_assert(sizeof(CameraRecord) == 13572, "CameraRecord is 13572 bytes");
	static_assert(offsetof(MotionRecord, groundRemap) == 52, "motion remap at 52");
	static_assert(offsetof(MotionRecord, currentEventName) == 140, "motion event name at 140");
	static_assert(offsetof(MotionRecord, chr) == 168, "motion CHR bytes at 168");
	static_assert(sizeof(MotionRecord) == 656, "MotionRecord is 656 bytes");
	static_assert(sizeof(MotionExtension) == 8 + 4 * 22, "MotionExtension holds 22 spans");
	static_assert(sizeof(JointsExtension) == 40 + 4 * 9, "JointsExtension holds 9 spans");
	static_assert(offsetof(JointsRecordHead, liveSceneId) == 52, "joints globals at 52");
	static_assert(offsetof(JointsRecordHead, camEye) == 80, "joints camera eye at 80");
	static_assert(offsetof(JointsRecordHead, chr) == 120, "joints CHR bytes at 120");
	static_assert(offsetof(JointsRecordHead, seqExit) == 444, "joints seqExit at 444");
	static_assert(sizeof(JointsRecordHead) == 688, "JointsRecordHead is 688 bytes");
	static_assert(sizeof(BattleExtension) == 124, "BattleExtension is 124 bytes");
	static_assert(offsetof(BattleStepRecord, battleRand) == 40, "battle step LCG at 40");
	static_assert(offsetof(BattleStepRecord, streams) == 48, "battle step streams at 48");
	static_assert(offsetof(BattleStepRecord, turnQueue) == 320, "battle step turn queue at 320");
	static_assert(offsetof(BattleStepRecord, cmdQueue) == 816, "battle step command queue at 816");
	static_assert(offsetof(BattleStepRecord, globals) == 1968, "battle step globals at 1968");
	static_assert(offsetof(BattleStepRecord, units) == 2360, "battle step units at 2360");
	static_assert(sizeof(BattleStepRecord) == 13396, "BattleStepRecord is 13396 bytes");
	static_assert(offsetof(BattleEventRecord, args) == 40, "battle event args at 40");
	static_assert(offsetof(BattleEventRecord, pendingGil) == 100, "battle event gil at 100");
	static_assert(offsetof(BattleEventRecord, streamsEntry) == 112, "battle event streams at 112");
	static_assert(offsetof(BattleEventRecord, blockEntry) == 656, "battle event blocks at 656");
	static_assert(offsetof(BattleEventRecord, class3Group) == 824, "battle event class 3 at 824");
	static_assert(sizeof(BattleEventRecord) == 1860, "BattleEventRecord is 1860 bytes");
	static_assert(sizeof(BattleEventExtension) == 16, "BattleEventExtension is 16 bytes");
	static_assert(sizeof(BattleDumpHead) == 344, "BattleDumpHead is 344 bytes");
	static_assert(offsetof(BattleDump, units) == 344, "battle dump units at 344");
	static_assert(offsetof(BattleDump, sides) == 123848, "battle dump side records at 123848");
	static_assert(offsetof(BattleDump, save) == 180392, "battle dump save block at 180392");
	static_assert(offsetof(BattleDump, work) == 207208, "battle dump work block at 207208");
	static_assert(offsetof(BattleDump, ai) == 215832, "battle dump AI globals at 215832");
	static_assert(offsetof(BattleDump, class3) == 215920, "battle dump class 3 at 215920");
	static_assert(sizeof(BattleDump) == 217968, "BattleDump is 217968 bytes");
	static_assert(sizeof(BattleDumpExtension) == 44, "BattleDumpExtension is 44 bytes");

} // namespace probe
