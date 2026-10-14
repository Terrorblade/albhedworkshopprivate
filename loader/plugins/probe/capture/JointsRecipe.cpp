// Recipe 4, joint transforms, for Project Zanarkand PROGRESS 10.4.
//
// reversing/CHARACTER_MOTION.md section 11.12. FFX_Mot_AdvanceFrame is observed at its one
// call site, entry and return, and each chosen CHR gets one record per side: the globals
// and camera slot fields the composer choice reads, the CHR's playback fields, the whole
// channel array and the composed joint dwords. Inside the call, the answers of the two
// composer queries and the playback block after FFX_Mot_StepSequence are kept for the
// return record, so a mismatch can be split between the VM, the channels and the composer.
//
// OFF BY DEFAULT and filtered to the controlled CHR by default, because a record is about
// 66 KB for c001 (136 joints), so one CHR is about 4 MB a second.
//
// Cost rules as elsewhere: nothing here makes a syscall. Every pointer the engine owns is
// dereferenced inside __try, the record is filled in place in the writer's buffer, and
// pointers are rebased in that buffer after the copy.

#include "capture/Capture.h"

#include <string.h>

#include "ProbeSettings.h"
#include "ffx/MotionPlayback.h"
#include "ffx/MotionStep.h"
#include "workshop/Log.h"

namespace probe
{
	namespace
	{

		namespace MP = ffx::MotPlay;

		// Step thread state for the one AdvanceFrame call being recorded.
		bool g_holding = false; // BeginRecord succeeded on entry for g_chr
		BYTE* g_chr = NULL;
		WORD g_call = 0;
		BYTE g_stationary = ComposerNotCalled;
		BYTE g_nearCamera = ComposerNotCalled;
		BYTE g_stationaryCalls = 0;
		BYTE g_nearCameraCalls = 0;
		BYTE g_sequenceCalls = 0;
		int g_sequenceResult = 0;
		bool g_sequenceSeen = false;
		BYTE g_sequenceBlock[PlaybackBlockBytes];

		template <typename T>
		T At(const BYTE* base, DWORD offset)
		{
			return *(const T*)(base + offset);
		}

		// Plain C on purpose: a function with __try cannot hold C++ objects that unwind.
		// Copies everything behind an engine pointer and fills the head's pointer fields.
		// Returns the chrFlags it learned.
		WORD CopyFromEngine(JointsRecordHead* head, BYTE* channelsOut, BYTE* jointsOut,
		    const BYTE* chr, DWORD capacity)
		{
			WORD flags = 0;
			__try
			{
				BYTE* at = head->chr;
				for (int i = 0; i < JointsChrSpanCount; ++i)
				{
					memcpy(at, chr + JointsChrSpans[i].offset, JointsChrSpans[i].bytes);
					at += JointsChrSpans[i].bytes;
				}

				const char* model = At<const char*>(chr, MP::ChrModelName);
				if (model)
					for (int i = 0; i < (int)sizeof(head->modelName) && model[i]; ++i)
						head->modelName[i] = model[i];

				const BYTE* data = At<const BYTE*>(chr, MP::ChrData);
				if (data)
					head->chrDataScale = At<float>(data, MP::ChrDataScale);

				const BYTE* skeleton = At<const BYTE*>(chr, MP::ChrSkeleton);
				const BYTE* channels = At<const BYTE*>(chr, MP::ChrChannels);
				const BYTE* joints = At<const BYTE*>(chr, MP::ChrJoints);
				head->bundleBase = (DWORD)(UINT_PTR)At<const BYTE*>(chr, MP::ChrMotionBundle);
				head->channelsBase = (DWORD)(UINT_PTR)channels;
				head->jointsBase = (DWORD)(UINT_PTR)joints;

				if (!skeleton)
				{
					flags |= JointsChrNoSkeleton;
					return flags;
				}
				const WORD count = At<WORD>(skeleton, MP::SkeletonJointCount);
				head->skeletonJoints = count;
				DWORD copy = count;
				if (copy > capacity)
				{
					copy = capacity;
					flags |= JointsChrTruncated;
				}
				head->jointsCopied = (WORD)copy;

				if (channels)
					memcpy(channelsOut, channels, copy * ChannelBlockBytes);
				else
					flags |= JointsChrNoChannels;

				if (joints)
				{
					for (DWORD j = 0; j < copy; ++j)
					{
						const BYTE* joint = joints + j * MP::JointStride;
						BYTE* out = jointsOut + j * JointEntryBytes;
						memcpy(out, joint + MP::JointComposedFirst, JointComposedBytes);
						out[JointComposedBytes] = joint[MP::JointSmoothFlag];
					}
				}
				else
				{
					flags |= JointsChrNoJoints;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				flags |= JointsChrFaulted;
			}
			return flags;
		}

		// Where CHR offset `offset` landed in the packed CHR bytes, or NULL.
		BYTE* SpanByte(BYTE* packed, WORD offset)
		{
			for (int i = 0; i < JointsChrSpanCount; ++i)
			{
				const ChrSpan& span = JointsChrSpans[i];
				if (offset >= span.offset && offset + 4 <= span.offset + span.bytes)
					return packed + (offset - span.offset);
				packed += span.bytes;
			}
			return NULL;
		}

		void RebaseBundle(BYTE* field, DWORD bundle)
		{
			DWORD value;
			memcpy(&value, field, 4);
			if (value && bundle)
			{
				value -= bundle;
				memcpy(field, &value, 4);
			}
		}

		// Null -> 0xFFFFFFFF, a channel -> its index, anything else is left raw. True when
		// the value was left raw.
		bool LinkToIndex(BYTE* field, DWORD channels, DWORD channelCount)
		{
			DWORD value;
			memcpy(&value, field, 4);
			if (value == 0)
			{
				value = 0xFFFFFFFF;
				memcpy(field, &value, 4);
				return false;
			}
			const DWORD delta = value - channels;
			if (channels == 0 || delta % ChannelEntryBytes != 0 || delta / ChannelEntryBytes >= channelCount)
				return true;
			value = delta / ChannelEntryBytes;
			memcpy(field, &value, 4);
			return false;
		}

		// The playback block's pointers, laid out as CHR offsets 0x70C..0x7FF.
		WORD RebaseBlock(BYTE* block, DWORD bundle, DWORD channels, DWORD channelCount)
		{
			const WORD bundleFields[] = { MP::ChrMotionEntry, MP::ChrMotionGroup, MP::ChrSeqPc, MP::ChrEventArray };
			for (int i = 0; i < 4; ++i)
				RebaseBundle(block + (bundleFields[i] - PlaybackBlockOffset), bundle);
			BYTE* streamList = block + (MP::ChrStreamList - PlaybackBlockOffset);
			return LinkToIndex(streamList, channels, channelCount) ? JointsChrLinkRaw : 0;
		}

		WORD Rebase(JointsRecordHead* head, BYTE* channelsOut, bool withSeqExit)
		{
			WORD flags = 0;
			const DWORD bundle = head->bundleBase;
			const DWORD channels = head->channelsBase;
			const DWORD channelCount = (DWORD)head->skeletonJoints * ChannelsPerJoint;
			if (!bundle)
				flags |= JointsChrNoBundle;

			const DWORD copied = (DWORD)head->jointsCopied * ChannelsPerJoint;
			for (DWORD c = 0; c < copied; ++c)
			{
				BYTE* channel = channelsOut + c * ChannelEntryBytes;
				if (LinkToIndex(channel + MP::ChannelNext, channels, channelCount))
					flags |= JointsChrLinkRaw;
				RebaseBundle(channel + MP::ChannelStreamStart, bundle);
				RebaseBundle(channel + MP::ChannelCursor, bundle);
			}

			BYTE* block = SpanByte(head->chr, PlaybackBlockOffset);
			if (block)
				flags |= RebaseBlock(block, bundle, channels, channelCount);
			if (withSeqExit)
				flags |= RebaseBlock(head->seqExit, bundle, channels, channelCount);
			return flags;
		}

		WORD PoolSlot(const BYTE* chr)
		{
			const BYTE* pool = NULL;
			int slots = 0;
			if (!ffx::ChrPoolForReading(&pool, &slots) || chr < pool)
				return NoSlot;
			const DWORD offset = (DWORD)(chr - pool);
			if (offset % ChrStride != 0 || offset / ChrStride >= (DWORD)slots)
				return NoSlot;
			return (WORD)(offset / ChrStride);
		}

		void RecordSide(BYTE* chr, int applyBlend, BYTE phase)
		{
			workshop::RecordFile* file = FileFor(KindJoints);
			const DWORD capacity = JointCapacity();
			if (!file || capacity == 0)
				return;
			BYTE* r = (BYTE*)workshop::ReserveRecord(file);
			if (!r)
				return; // the budget is spent and the drop is counted

			JointsRecordHead* head = (JointsRecordHead*)r;
			BYTE* channelsOut = r + sizeof(JointsRecordHead);
			BYTE* jointsOut = channelsOut + capacity * ChannelBlockBytes;

			FillPrefix(&head->prefix, KindJoints, g_call, phase);
			head->chrPointer = (DWORD)(UINT_PTR)chr;
			head->applyBlend = applyBlend;
			head->slot = PoolSlot(chr);

			ffx::MotionPlaybackGlobals g;
			if (ffx::ReadMotionPlaybackGlobals(&g))
			{
				head->prefix.flags |= JointsFlagGlobals;
				head->liveSceneId = g.liveSceneId;
				head->storyProgress = g.storyProgress;
				head->btlSubPhase = g.btlSubPhase;
				head->motKeepSpeed = g.motKeepSpeed;
				head->currentEventId = g.currentEventId;
				head->stepScale88 = g.stepScale88;
				head->motHalfSpeed = g.motHalfSpeed;
				head->atelContextByte = g.atelContextByte;
				head->atelContextValid = g.atelContextValid ? 1 : 0;
				head->camSlot = (DWORD)(UINT_PTR)g.camSlot;
				head->camSlotIndex = g.camSlotIndex;
				if (g.camValid)
				{
					head->prefix.flags |= JointsFlagCamera;
					memcpy(head->camEye, g.camEye, sizeof(head->camEye));
					head->camPitchWhenSet = g.camPitchWhenSet;
					head->camPitchWhenClear = g.camPitchWhenClear;
					head->camPitchSelect = g.camPitchSelect;
				}
			}
			else
			{
				head->camSlotIndex = -1;
			}

			WORD flags = CopyFromEngine(head, channelsOut, jointsOut, chr, capacity);
			if (g.controlledChr && g.controlledChr == chr)
				flags |= JointsChrControlled;
			if (!(flags & (JointsChrNoSkeleton | JointsChrNoChannels)))
				head->prefix.flags |= JointsFlagChannels;
			if (!(flags & (JointsChrNoSkeleton | JointsChrNoJoints)))
				head->prefix.flags |= JointsFlagJoints;

			if (phase == 1)
			{
				head->stationary = g_stationary;
				head->nearCamera = g_nearCamera;
				head->stationaryCalls = g_stationaryCalls;
				head->nearCameraCalls = g_nearCameraCalls;
				head->stepSequenceCalls = g_sequenceCalls;
				head->stepSequenceResult = g_sequenceResult;
				if (g_sequenceSeen)
				{
					memcpy(head->seqExit, g_sequenceBlock, sizeof(head->seqExit));
					head->prefix.flags |= JointsFlagSeqExit;
				}
			}
			else
			{
				head->stationary = ComposerNotCalled;
				head->nearCamera = ComposerNotCalled;
			}

			// A faulted copy is left as it is. Rebasing half a copy would only add confusion.
			if (!(flags & JointsChrFaulted))
			{
				flags |= Rebase(head, channelsOut, phase == 1 && g_sequenceSeen);
				head->prefix.flags |= JointsFlagRebased;
			}
			head->chrFlags = flags;
			workshop::CommitRecord(file);
		}

		bool Chosen(const BYTE* chr)
		{
			const long filter = settings.jointsChr;
			if (filter == JointsFilterEvery)
				return true;
			if (filter == JointsFilterControlled)
			{
				ffx::MotionPlaybackGlobals g;
				return ffx::ReadMotionPlaybackGlobals(&g) && g.controlledChr == chr;
			}
			// The engine is about to read this CHR, so its first word is safe to read.
			return filter >= 0 && *(const WORD*)chr == (WORD)filter;
		}

		void __cdecl OnAdvanceFrame(BYTE* chr, int applyBlend, bool afterCall)
		{
			if (!afterCall)
			{
				g_holding = false;
				if (!settings.joints || !chr || !BeginRecord())
					return;
				if (!FileFor(KindJoints) || !Chosen(chr))
				{
					EndRecord();
					return;
				}

				g_holding = true;
				g_chr = chr;
				// The same index as this sub step's motion records, which the motion hook
				// counted on its way in.
				g_call = cursor.motionCallInStep ? (WORD)(cursor.motionCallInStep - 1) : 0;
				g_stationary = ComposerNotCalled;
				g_nearCamera = ComposerNotCalled;
				g_stationaryCalls = 0;
				g_nearCameraCalls = 0;
				g_sequenceCalls = 0;
				g_sequenceResult = 0;
				g_sequenceSeen = false;
				RecordSide(chr, applyBlend, 0);
				return;
			}

			if (!g_holding || chr != g_chr)
				return;
			g_holding = false;
			RecordSide(chr, applyBlend, 1);
			g_chr = NULL;
			EndRecord();
		}

		void __cdecl OnStepSequence(BYTE* chr, int result)
		{
			if (!g_holding || chr != g_chr)
				return;
			if (g_sequenceCalls < 0xFF)
				++g_sequenceCalls;
			g_sequenceResult = result;
			// The CHR is in the pool and StepSequence just wrote these bytes, so no guard.
			memcpy(g_sequenceBlock, chr + PlaybackBlockOffset, sizeof(g_sequenceBlock));
			g_sequenceSeen = true;
		}

		void __cdecl OnComposerQuery(BYTE* chr, int which, BYTE result)
		{
			if (!g_holding || chr != g_chr)
				return;
			if (which == ffx::ComposerQueryStationary)
			{
				g_stationary = result;
				if (g_stationaryCalls < 0xFF)
					++g_stationaryCalls;
			}
			else
			{
				g_nearCamera = result;
				if (g_nearCameraCalls < 0xFF)
					++g_nearCameraCalls;
			}
		}

	} // namespace

	bool JointsSpansConsistent()
	{
		int sum = 0;
		int previousEnd = 0;
		for (int i = 0; i < JointsChrSpanCount; ++i)
		{
			if (JointsChrSpans[i].offset < previousEnd || JointsChrSpans[i].offset + JointsChrSpans[i].bytes > (int)ChrStride)
			{
				workshop::Log("joints recipe: span %d at +0x%03X overlaps the one before it or runs "
				              "off the CHR, joints recording is off",
				    i, JointsChrSpans[i].offset);
				return false;
			}
			previousEnd = JointsChrSpans[i].offset + JointsChrSpans[i].bytes;
			sum += JointsChrSpans[i].bytes;
		}
		if (sum != JointsChrBytes)
		{
			workshop::Log("joints recipe: the spans add up to %d bytes and the record holds %d, "
			              "joints recording is off",
			    sum, JointsChrBytes);
			return false;
		}
		return true;
	}

	DWORD JointsExtensionBytes(void* out, DWORD capacity, DWORD jointCapacity)
	{
		JointsExtension e;
		e.chrStride = ChrStride;
		e.jointCapacity = jointCapacity;
		e.channelEntryBytes = ChannelEntryBytes;
		e.channelsPerJoint = ChannelsPerJoint;
		e.jointSourceStride = ffx::MotPlay::JointStride;
		e.jointDwordFirst = ffx::MotPlay::JointComposedFirst / 4;
		e.jointDwordCount = JointComposedBytes / 4;
		e.jointEntryBytes = JointEntryBytes;
		e.headBytes = sizeof(JointsRecordHead);
		e.spanCount = (DWORD)JointsChrSpanCount;
		memcpy(e.spans, JointsChrSpans, sizeof(e.spans));
		if (capacity < sizeof(e))
			return 0;
		memcpy(out, &e, sizeof(e));
		return sizeof(e);
	}

	bool InstallJointsRecipe()
	{
		static_assert(ffx::MotPlay::JointComposedBytes == JointComposedBytes, "joint dwords agree with the kit");
		static_assert(ffx::MotPlay::ChannelBytes == ChannelEntryBytes, "channel size agrees with the kit");
		static_assert(ffx::MotPlay::ChannelsPerJoint == ChannelsPerJoint, "channels a joint agree with the kit");

		const bool advance = ffx::HookAdvanceFrame(&OnAdvanceFrame);
		const bool sequence = ffx::HookStepSequence(&OnStepSequence);
		const bool composer = ffx::HookComposerQueries(&OnComposerQuery);
		workshop::Log("joints recipe: AdvanceFrame %s, StepSequence %s, composer queries %s",
		    advance ? "hooked" : "NOT HOOKED", sequence ? "hooked" : "not hooked, seqExit stays empty",
		    composer ? "hooked" : "not hooked, the composer answers stay 0xFF");
		return advance;
	}

} // namespace probe
