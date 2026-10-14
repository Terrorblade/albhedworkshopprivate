#pragma once

#include <windows.h>

// The character motion step: FFX_Ch_UpdateMotionAll and the globals it reads besides the
// CHR pool. Turn, velocity, integrate, collide, ground and water clamp, and the motion
// clock, all in one call per sub step.
//
// Addresses are RVAs, the IDA VA minus 0x00400000, each checked in the IDB on 2026-10-13.
// The derivation is reversing\CHARACTER_MOTION.md, section 10.4 of which is the capture
// recipe the probe plugin implements.
//
// NOT DECLARED HERE, because another area owns them:
//   ChrArray, ChrCount, ControlledChr     addresses\Character.h
//   WalkmeshScale, WalkmeshTriCount       addresses\Character.h
//   GroundTypeRemapBiasedBase             addresses\Character.h (codes 48..63 only)
//   MotionKillSwitch                      addresses\Character.h (g_ffxDbgChrUpdateFrozen)
//   PlayerDebugNoClip                     addresses\Debug.h
//   StepScale88, MotionFixedStepSeconds   addresses\MainLoop.h
//   EvCurrentEventId, EvCurrentEventName  addresses\Minigames.h
//   LiveSceneId                           addresses\WorldState.h
//   BtlSubPhase                           addresses\Battle.h
//   AtelContextPtr, AtelContextArray      addresses\Atel.h
//   CameActiveSlot                        addresses\Character.h
//
// The second half is motion PLAYBACK, FFX_Mot_AdvanceFrame and the joint composers, from
// CHARACTER_MOTION.md section 11. Its capture recipe is section 11.12.

namespace ffx
{
	namespace Rva
	{

		// int __cdecl (float dt). The IDB types the argument as CHR * and it is not: the
		// one call site, 0x82107D in FFX_MainStep, pushes g_ffxMotionStepDt, the constant
		// 0.033373334 (0x3D08B27A). Returns early with the freeze byte's value when
		// MotionKillSwitch is set. Gated at the call site by MotionUpdateEnabled.
		//
		// Prologue 55 8B EC 83 EC 10, six bytes with no relative operand, so it detours.
		// The call after it is the freeze test and is not stolen.
		const DWORD ChUpdateMotionAll = 0x00432E10;

		// BYTE, non-zero lets FFX_MainStep call ChUpdateMotionAll at all.
		const DWORD MotionUpdateEnabled = 0x00EFB8FF; // g_ffxChrMotionUpdateEnabled

		// float. The water surface the pass 3 clamp compares m_posY - 8.5 against, and its
		// running maximum, which the step itself raises when the level goes above it.
		const DWORD WaterLevel = 0x00F009D8;    // g_ffxWaterLevel
		const DWORD WaterLevelMax = 0x00F009DC; // g_ffxWaterLevelMax

		// The motion clock modifiers FFX_Mot_AdvanceFrame reads.
		const DWORD MotKeepSpeed = 0x00EFBC34; // BYTE, g_ffxMotKeepSpeed, the KeepSp scale
		const DWORD MotHalfSpeed = 0x00F017F8; // int, g_ffxMotHalfSpeed

		// ---------------------------------------------------------------------------
		// Playback. Every function here has exactly one caller, so each is hooked at that
		// call site rather than detoured. One dword changes, nothing is relocated, and
		// IsNearlyStationary, which opens with a rel32 call at +3, needs no stolen bytes.
		// ---------------------------------------------------------------------------

		// void __cdecl (CHR *chr, int applyBlend). Called once per CHR from the third pass
		// of ChUpdateMotionAll, at the call site below, with applyBlend = m_flags1 bit 24.
		// Prologue 55 8B EC 53 56.
		const DWORD MotAdvanceFrame = 0x00438C90;
		const DWORD MotAdvanceFrameCallSite = 0x00433273;

		// int __cdecl (CHR *chr). The sequence VM, run until it blocks. Only AdvanceFrame
		// calls it, after m_frameDelta is stored and before the channels are evaluated.
		const DWORD MotStepSequence = 0x004377C0;
		const DWORD MotStepSequenceCallSite = 0x00438E05;

		// int __cdecl (CHR *chr). Picks the joint composer and writes the joint transforms.
		// Only AdvanceFrame calls it. Listed for the two call sites inside it.
		const DWORD MotBuildJointTransforms = 0x00438F70;

		// bool __cdecl (CHR *chr), the answer in al. False in battle or while the ATEL
		// context's byte 0 bit 2 is set, else true when the body centre moved less than 2.0
		// on every axis. Starts with a rel32 call, so a detour could not take its prologue.
		const DWORD ChIsNearlyStationary = 0x00439200;
		const DWORD ChIsNearlyStationaryCallSite = 0x00438FE5;

		// char __cdecl (CHR *chr). The body centre within 30 (50 for a "c10" model) of the
		// active camera slot's eye and the slot pitch above -pi/4.
		const DWORD ChIsNearCameraForSmoothing = 0x00439140;
		const DWORD ChIsNearCameraCallSite = 0x00438FF2;

		// WORD, g_ffxStoryProgress, the save block's story word. FFX_Story_GetProgress
		// 0x787360 returns it, and BuildJointTransforms reads it on scene 60.
		const DWORD StoryProgress = 0x00D2D67C;

		inline const DWORD* MotionRvaList(int* count)
		{
			static const DWORD list[] = {
				ChUpdateMotionAll,
				MotionUpdateEnabled,
				WaterLevel,
				WaterLevelMax,
				MotKeepSpeed,
				MotHalfSpeed,
				MotAdvanceFrame,
				MotAdvanceFrameCallSite,
				MotStepSequence,
				MotStepSequenceCallSite,
				MotBuildJointTransforms,
				ChIsNearlyStationary,
				ChIsNearlyStationaryCallSite,
				ChIsNearCameraForSmoothing,
				ChIsNearCameraCallSite,
				StoryProgress,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
