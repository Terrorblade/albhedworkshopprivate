#pragma once

#include <windows.h>

// The character motion step, FFX_Ch_UpdateMotionAll 0x832E10, read and observed.
//
// One call per sub step from FFX_MainStep, with the fixed dt 0.033373334. For every in use
// CHR whose hide gate passes it turns, picks the clip, turns m_speed and m_moveDir into a
// velocity, then FFX_Ch_ResolveCollisionsAll integrates and collides the lot, then the
// ground and water clamp, then the motion clock. Nothing in it reads the player global,
// which is the finding the whole co-op mod stands on. reversing\CHARACTER_MOTION.md is the
// derivation and its section 10.4 is the recipe this header serves.
//
// The CHR pool is a heap block behind a module global. ChrPoolForReading validates the
// whole span once and again only when the base or the count changes, which happens on a
// map load and not per step, so a per step recorder makes no syscall here.

namespace ffx
{

	// Everything the step reads from outside the CHRs, in one read.
	struct MotionGlobals
	{
		const BYTE* chrArray; // g_ffxChrArray, the pool base
		int chrCount;         // g_ffxChrCount, slots in the pool
		const BYTE* controlledChr;
		float walkmeshScale;
		int walkmeshTriCount;
		float waterLevel;
		float waterLevelMax;
		int groundRemap[16]; // the surface code remap for codes 48..63, the only ones read
		int debugNoClip;
		DWORD stepScale88;
		int motHalfSpeed;
		BYTE chrUpdateFrozen; // non-zero makes the step return at once
		BYTE motKeepSpeed;
		BYTE motionUpdateEnabled; // zero and FFX_MainStep never calls the step
		int currentEventId;
		int liveSceneId;
		char currentEventName[16]; // the first 16 bytes of the 256 byte buffer, "bs/bsil0100"
	};
	bool ReadMotionGlobals(MotionGlobals* out);

	// The pool base and slot count, with the whole span validated once per change. False,
	// and both outputs NULL and 0, when the pool is not there or not readable.
	bool ChrPoolForReading(const BYTE** base, int* count);

	// ---------------------------------------------------------------------------
	// The observer hook, a detour on FFX_Ch_UpdateMotionAll. The observer runs before the
	// original with afterCall false, then after it with afterCall true, so a recorder gets
	// the CHRs on both sides of exactly one step. It cannot change anything and the result
	// goes back unchanged.
	//
	// Chains behind another plugin's detour, see workshop/Detour.h. One observer per
	// module, installing again replaces it.
	// ---------------------------------------------------------------------------
	typedef void(__cdecl* MotionStepObserverFn)(float dt, bool afterCall);
	bool HookUpdateMotionAll(MotionStepObserverFn observer);
	bool UpdateMotionAllHookInstalled();

} // namespace ffx
