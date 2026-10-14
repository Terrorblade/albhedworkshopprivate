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
	};
	const int KindCount = 5; // kinds are 1..KindCount-1, for arrays indexed by kind

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

} // namespace probe
