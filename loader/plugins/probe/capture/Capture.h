#pragma once

#include <windows.h>

#include "capture/FixtureFormat.h"
#include "workshop/RecordFile.h"

// The capture session and the gate every recipe goes through.
//
// TWO THREADS AND ONE RULE. The recipes run on the game's step thread, inside the hooks.
// Start and stop run on the probe's control thread. The rule between them is a counter:
// a recipe calls BeginRecord before it touches a file and EndRecord after, and stop
// clears the recording flag and then waits for the counter to reach zero before it
// closes anything. So a file is never closed under a recipe that is writing to it, and
// the step thread never waits on anything.
//
// A recipe that records across the original call, entry before and exit after, holds
// the counter across the call too. Stop then waits that long, which is a few
// milliseconds, and the record is never cut in half.

namespace probe
{

	// ---------------------------------------------------------------------------
	// Step thread
	// ---------------------------------------------------------------------------

	// False when not recording. True means the caller must call EndRecord exactly once.
	bool BeginRecord();
	void EndRecord();

	// The open file for a kind, or NULL when that recipe is off for this session. Only
	// valid between BeginRecord and EndRecord.
	workshop::RecordFile* FileFor(FixtureKind kind);

	// The next seq for a kind's file. Step thread only.
	DWORD NextSeq(FixtureKind kind);

	// Joints each record of the open joints file has room for. Only valid between
	// BeginRecord and EndRecord, and 0 when this session has no joints file.
	DWORD JointCapacity();

	// The step bookkeeping every record's prefix is built from. Kept by the pacing hook
	// whether or not a capture is running, so the first record of a capture already has
	// the right ordinal.
	struct StepCursor
	{
		DWORD pacingOrdinal;       // FFX_StepPacing calls since the plugin loaded
		DWORD lastFrameTick;       // the presented frame of the last pacing call
		WORD pacingCallInFrame;    // 0 for the first pacing call of a presented frame
		WORD cameraCallInStep;     // ActivateScreen calls since the last pacing call
		WORD motionCallInStep;     // UpdateMotionAll calls since the last pacing call
		DWORD sessionFirstOrdinal; // pacingOrdinal when this capture started
	};
	extern StepCursor cursor;

	// Fills a prefix from the cursor and the two engine counters. callIndex and phase are
	// the caller's.
	void FillPrefix(RecordPrefix* prefix, FixtureKind kind, WORD callIndex, BYTE phase);

	// Called by the pacing hook on every call, before it records. Once probe.max_steps
	// steps have been recorded it stops recording on the spot and asks the control thread
	// to close the files.
	void NoteStepForLimit();

	// ---------------------------------------------------------------------------
	// Control thread
	// ---------------------------------------------------------------------------

	bool StartCapture();
	void StopCapture(const char* why);
	bool CaptureRunning();

	// True, once, when the step limit asked for a stop.
	bool TakeLimitStopRequest();

	// ---------------------------------------------------------------------------
	// The recipes, each in its own file. Install puts the hook in, unconditionally and
	// once, and the hook body checks BeginRecord and its own setting every call.
	// ---------------------------------------------------------------------------
	bool InstallPacingRecipe();
	bool InstallCameraRecipe();
	bool InstallMotionRecipe();
	bool InstallJointsRecipe();

	// The kind specific header extension each recipe writes after FileHeader.
	DWORD CameraExtensionBytes(void* out, DWORD capacity);
	DWORD MotionExtensionBytes(void* out, DWORD capacity);
	DWORD JointsExtensionBytes(void* out, DWORD capacity, DWORD jointCapacity);

	// Checks the span table against the record size. Logged and false when they disagree,
	// in which case motion recording stays off.
	bool MotionSpansConsistent();
	bool JointsSpansConsistent();

} // namespace probe
