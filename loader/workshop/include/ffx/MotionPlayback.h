#pragma once

#include <windows.h>

// Motion playback, FFX_Mot_AdvanceFrame 0x838C90 and the joint composers, read and observed.
//
// AdvanceFrame runs once per CHR from the third pass of FFX_Ch_UpdateMotionAll. It steps
// the sequence VM, evaluates and blends the 44 byte channels, and composes the joint
// transforms into dwords 57..79 of each 352 byte joint. reversing\CHARACTER_MOTION.md
// section 11 is the derivation and 11.12 is the capture recipe this header serves.
//
// Every function observed here has one caller, so the hooks are call site patches rather
// than detours. All of them run on the step thread.

namespace ffx
{

	// CHR, joint, channel and camera slot offsets the playback code uses. All [V] in IDA.
	namespace MotPlay
	{
		const DWORD ChrModelName = 0x004;      // const char *, the .chr model name ("c001")
		const DWORD ChrFlags1 = 0x194;         // m_flags1
		const DWORD ChrSkeleton = 0x1C0;       // skeleton header, joint count u16 at +0x0A
		const DWORD ChrData = 0x1C4;           // CHRDATA, the model scale float at +0x38
		const DWORD ChrJoints = 0x328;         // joint array, JointStride apart
		const DWORD ChrMotionEntry = 0x70C;    // into the bundle
		const DWORD ChrMotionGroup = 0x710;    // into the bundle
		const DWORD ChrSeqPc = 0x714;          // into the bundle
		const DWORD ChrChannels = 0x748;       // channel array, 9 per skeleton joint
		const DWORD ChrStreamList = 0x74C;     // first streamed channel, into the channel array
		const DWORD ChrMotionBundle = 0x7E8;   // the registered .mgrp header
		const DWORD ChrEventArray = 0x7F4;     // into the bundle, m_eventCount u16 at 0x7F0
		const DWORD ChrComposeAlways = 0x871;  // byte, non-zero forces the smoothed composer
		const DWORD ChrComposeEnabled = 0x870; // byte, zero forces the plain composer

		const DWORD SkeletonJointCount = 0x0A; // u16
		const DWORD ChrDataScale = 0x38;       // float, times 0.001f is the translation scale

		const DWORD JointStride = 352;
		const DWORD JointComposedFirst = 0xE4; // dword 57
		const DWORD JointComposedBytes = 92;   // dwords 57..79
		const DWORD JointSmoothFlag = 0x144;   // byte 324, the Auto composer's switch

		const DWORD ChannelBytes = 44;
		const DWORD ChannelsPerJoint = 9;
		const DWORD ChannelNext = 0x00;        // next streamed channel, into the array
		const DWORD ChannelStreamStart = 0x08; // into the bundle
		const DWORD ChannelCursor = 0x0C;      // into the bundle

		const DWORD SlotPitchSelect = 0x64D; // byte, picks which pitch below is read
		const DWORD SlotEye = 0x65C;         // float[4], subtracted with FFX_Vec4_Sub
		const DWORD SlotPitchWhenSet = 0x6B0;
		const DWORD SlotPitchWhenClear = 0x6D0;
	} // namespace MotPlay

	// Everything AdvanceFrame and the composer choice read from outside the CHR.
	struct MotionPlaybackGlobals
	{
		const BYTE* controlledChr;
		int liveSceneId;
		WORD storyProgress;
		BYTE btlSubPhase;
		BYTE motKeepSpeed;
		int currentEventId;
		DWORD stepScale88;
		int motHalfSpeed;

		// The byte the selected ATEL context pointer points at. Valid only when the pointer
		// lands inside the context array, which it always should.
		BYTE atelContextByte;
		bool atelContextValid;

		// Through g_ffxCameActiveSlot. Valid only when the slot is one of the three.
		const BYTE* camSlot;
		int camSlotIndex;
		float camEye[4];
		BYTE camPitchSelect;
		float camPitchWhenSet;
		float camPitchWhenClear;
		bool camValid;
	};
	bool ReadMotionPlaybackGlobals(MotionPlaybackGlobals* out);

	// ---------------------------------------------------------------------------
	// Observers. Each runs on the step thread, cannot change anything, and the original
	// always runs. One observer per hook per module, installing again replaces it.
	// ---------------------------------------------------------------------------

	// Before the original with afterCall false, after it with afterCall true.
	typedef void(__cdecl* AdvanceFrameObserverFn)(BYTE* chr, int applyBlend, bool afterCall);
	bool HookAdvanceFrame(AdvanceFrameObserverFn observer);

	// After FFX_Mot_StepSequence returns, with what it returned.
	typedef void(__cdecl* StepSequenceObserverFn)(BYTE* chr, int result);
	bool HookStepSequence(StepSequenceObserverFn observer);

	// After either composer query returns. which is ComposerQueryStationary or
	// ComposerQueryNearCamera. NearCamera is only asked when Stationary said true.
	enum ComposerQuery
	{
		ComposerQueryStationary = 0,
		ComposerQueryNearCamera = 1,
	};
	typedef void(__cdecl* ComposerQueryObserverFn)(BYTE* chr, int which, BYTE result);
	bool HookComposerQueries(ComposerQueryObserverFn observer);

} // namespace ffx
