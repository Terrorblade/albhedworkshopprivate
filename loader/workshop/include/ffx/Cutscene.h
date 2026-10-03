#pragma once

#include <windows.h>

// Cutscenes, dialogue and FMV.
//
// What a co-op plugin needs to answer four questions: is a cutscene running, is
// a dialogue box waiting for somebody to press a button, is the frame pacing
// about to become non-deterministic, and may I let either player advance the
// text.
//
// Every reader here goes through workshop::Readable and returns false rather
// than faulting, because all of it is meant to be callable from the frame path.
//
// ===========================================================================
// THE ONE-PARAGRAPH VERSION
// ===========================================================================
//
// An ATEL cutscene replicates for free. The script VM steps inside the
// FFX_MainStep sub-step loop, once per simulation step, and never reads a
// clock. An FMV does not replicate at all and has to be a barrier, because it
// is WebM decoded on its own thread, but that costs nothing because no
// simulation runs while it plays. In between sits the one genuine hazard: a
// VOICED line switches the frame pacing to a timing track read off the
// millisecond clock, and a machine that is ahead of its track BUSY-WAITS. Even
// there, the content of each step comes out of the data file rather than the
// clock, so lockstep wins by owning the step count rather than by rewriting the
// pacing.
//
// ===========================================================================
// THE THREE THINGS A PLAYER CALLS A CUTSCENE
// ===========================================================================
//
//   an ATEL event scene   the opcode script VM. One step per simulation step.
//                         Deterministic given a synced RNG stream.
//   a voiced line in one  the same VM, but FFX_StepPacing switches to the
//                         SyncData timing track and paces off the clock.
//   an FMV                a Phyre WebM player on its own decode thread.
//                         No simulation runs. A barrier, not a sync problem.
//
// A dialogue box is a fourth thing, driven by ATEL syscalls but stepped by its
// own module inside the same sub-step loop. It can be up outside a cutscene,
// which is why DialogueState() is a separate question from CutsceneActive().
//
// ===========================================================================
// PACING MODE 1, WHICH WAS THE PROJECT'S BIGGEST OPEN QUESTION
// ===========================================================================
//
// FFX_StepPacing_LoadTimingTrack is the ONLY writer of the pacing mode in the
// whole binary, and FFX_StepPacing calls it every frame that the pending step
// count drained to zero, before testing the mode. So mode 1 means exactly one
// thing and it is re-derived every frame:
//
//   a SyncData timing track is selected and has entries left
//
// A track is selected by an ATEL call-actor-script opcode whose operand tuple
// matches an entry in the current map's sync table. The tables beside it in the
// same file are read by the script's voice commands, so a timing track is a
// voiced line keeping the simulation lined up with its streamed audio. A
// cutscene with no voiced line never leaves mode 0.
//
// What mode 1 does: it compares elapsed milliseconds against the track's due
// time, takes extra simulation steps when it is behind, and SPINS in a
// for (i = 0; i < 10000; ++i) loop when it is more than 100 ms ahead.
//
// What mode 1 does NOT do: make a step's contents depend on the clock. While
// mode 1 is active, FFX_MainStep_PublishStepScale takes the per-step motion
// scale straight out of the track data rather than from the game clock delta.
// Both machines consume the same entries in the same order and get the same
// scale. Only the step count per presented frame differs.
//
// SO THE RECIPE IS: let the track run, own the step count, and tell the track
// about any time the gate stole. The last part is the bit that is easy to miss
// and it has a shipped primitive:
//
//   if (ffx::PacingMode() == 1) {
//       DWORD before = GetTickCount();
//       ... hold the simulation while waiting for the peer ...
//       ffx::AddTimingTrackPauseMs(GetTickCount() - before);
//   }
//
// Without that, the track believes it fell behind by however long the gate held
// and burns catch-up steps the instant the gate opens, which looks like the
// cutscene lurching forward.
//
// ONE MORE TRAP. The busy-wait lives inside FFX_StepPacing, inside
// FFX_MainStep, inside animate. A gate in the animate hook runs BEFORE it, so
// that is fine. A gate in the narrow FFX_MainStep detour runs AFTER
// FFX_StepPacing has already spun, so a machine ahead of its track burns up to
// 100 ms before the gate is even asked. Use the animate hook during a cutscene.
//
// ===========================================================================
// THE DIALOGUE ADVANCE, AND WHY IT IS EASY
// ===========================================================================
//
// One global, one bit, no port and no player id:
//
//   the shared menu pad block + 0x14, bit ffx::Btn::Circle
//
// read by FFX_MesWin_OnConfirmPressed as a plain edge test. The block is filled
// by FFX_MesWin_SamplePadPort0 from pad PORT 0 ONLY, and FFX_MesWin_StepAll
// samples and then immediately walks the 8 windows, so a value written inside
// a hook on the sampler is exactly what the advance read sees in the same step.
//
//   bool __cdecl MyMesWinPadHook(void)   // installed by HookDialoguePad
//   {
//       if (PeerPressedConfirm()) ffx::InjectDialogueConfirm(true);
//       return true;
//   }
//
// The sampler's prologue is a clean five bytes with no branch and no absolute
// address, which is why it is the hook point rather than the confirm handler
// (absolute address in the prologue) or the step function (relative call at
// offset 2).
//
// TWO HAZARDS WORTH KNOWING EVEN THOUGH NEITHER BREAKS THE PLAN:
//   - port 0 is hardcoded, so the second pad never reaches a box on its own
//   - the auto-repeat timers are wall-clock derived, first repeat 0.2333 s then
//     0.1333 s, so HELD DIRECTIONS in a choice list are not deterministic. A
//     single confirm press is, because that is a pure edge test.
//
// The derivation and the evidence are in reversing\CUTSCENE.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Field offsets. Not addresses, so they live here rather than in
	// addresses/Cutscene.h.
	// ---------------------------------------------------------------------------

	// The 0x44-byte SyncData manager. The cursor at +8 being non-null is the
	// precondition for pacing mode 1.
	namespace SyncDataLayout
	{
		const DWORD TrackDataArray = 0x04; // void **, parallel with the key arrays
		const DWORD TrackCursor = 0x08;    // float *, THE thing that arms mode 1
		const DWORD LoadedMapId = 0x0C;    // int
		const DWORD SelectEntries = 0x10;  // int, track-select table row count
		const DWORD VoiceEntries = 0x14;   // int, voice-sync table row count
		const DWORD SyncListCount = 0x18;  // int
		const DWORD MapHasSyncData = 0x1C; // int, non-zero means this map has a file
		const DWORD LoadFinished = 0x20;   // int, the async load completed
		const DWORD SyncListArray = 0x40;  // int *, the map ids that have sync data
	} // namespace SyncDataLayout

	// One of the 8 ATEL message window records. Stride 44, and the whole array of 8
	// is double buffered with a 352-byte stride, so always go through
	// MessageWindow() rather than indexing the base.
	namespace MesWinRecord
	{
		const DWORD Stride = 44;
		const DWORD BufferStride = 352;
		const DWORD RectX = 0x00;        // short
		const DWORD RectY = 0x02;        // short
		const DWORD RectW = 0x04;        // short
		const DWORD RectH = 0x06;        // short
		const DWORD TextPtr = 0x08;      // char *, from FFX_Atel_GetMessageText
		const DWORD TextPtrAlso = 0x0C;  // char *, the same value
		const DWORD State = 0x14;        // WORD (byte 20), see MesWinState below
		const DWORD Attr = 0x16;         // WORD (byte 22), the message table's attribute
		const DWORD StyleIndex = 0x1C;   // BYTE (byte 28)
		const DWORD Flags = 0x1D;        // BYTE (byte 29), see MesWinFlag below
		const DWORD ChosenOption = 0x1E; // short (byte 30), -1 for none
		                                 // Bytes 31 through 35 are the choice window's geometry and are passed straight
		                                 // to the draw module. Which is which was NOT established, so they are
		                                 // deliberately not named here. See reversing\CUTSCENE.md.
	} // namespace MesWinRecord

	// Values of MesWinRecord::State. 0, 1 and 3 were read directly from the writes.
	// 2 and 4 are inferred from the core:125 wait handler, which reports completion
	// on 0 and 4 and reads the player's choice on 2. See reversing\CUTSCENE.md.
	namespace MesWinState
	{
		const WORD Idle = 0;
		const WORD Shown = 1;
		const WORD WaitingOnPlayer = 2; // inferred
		const WORD Closing = 3;
		const WORD Finished = 4; // inferred
	} // namespace MesWinState

	// Bits of MesWinRecord::Flags. The byte is computed by the show syscall from its
	// operand. Three of these are proved by what the code does with them, two are
	// inferred from which draw call they select.
	namespace MesWinFlag
	{
		const BYTE FullRect = 0x08;    // proved: selects the 4-short rect draw over the 2-short one
		const BYTE BlocksSoft = 0x10;  // proved: counts as "a box is up" but does not wait
		const BYTE BlocksWait = 0x22;  // proved: waits for a button before it will close
		const BYTE ChoiceArmed = 0x20; // proved: the choice wait syscall ORs this in
		const BYTE SubDrawA = 0x02;    // inferred, selects one extra draw call
		const BYTE SubDrawB = 0x40;    // inferred, selects a different one and suppresses 0x02
	} // namespace MesWinFlag

	// The two offsets this area needs inside the shared menu pad block. The full
	// layout is the menu system's, in addresses\MenuSystem.h.
	namespace MesWinPad
	{
		const DWORD Held = 0x12;    // WORD, the level mask
		const DWORD Pressed = 0x14; // WORD, the newly-pressed edge. THE ADVANCE READ.
	} // namespace MesWinPad

	// PhyFMVPlayerManager, the fields read out of FFX_Fmv_CreatePlayer.
	namespace FmvManager
	{
		const DWORD FilePath = 0x4F0;  // char[], the resolved video path
		const DWORD IsPlaying = 0x6D0; // BYTE, what FFX_Fmv_IsPlaying reads
		const DWORD HasPlayer = 0x6D2; // BYTE, a player object exists
		const DWORD VideoId = 0x6DC;   // int
		const DWORD WidthF = 0x6F4;    // float
		const DWORD HeightF = 0x6F8;   // float
	} // namespace FmvManager

	// The lazily built 0x340-byte FMV state object behind FFX_Fmv_GetPlaybackState.
	namespace FmvStateLayout
	{
		const DWORD State = 0x000; // int, returned when neither flag is set
		const DWORD FlagA = 0x338; // BYTE, either flag set means "returns 2"
		const DWORD FlagB = 0x33C; // BYTE
	} // namespace FmvStateLayout

	// The ATEL context field this area cares about. The rest is addresses\Atel.h.
	namespace AtelContext
	{
		const DWORD StepCounter = 500; // int, ++ per step while the context is active
	} // namespace AtelContext

	// ---------------------------------------------------------------------------
	// IS A CUTSCENE RUNNING
	//
	// There is no single boolean in the binary and the obvious candidate is a dead
	// end: g_ffxPlayerControlEnabled's only writer has zero call sites, so it is an
	// exported plugin API the shipped game never uses.
	//
	// What does exist is four cheap reads, three of which the engine itself
	// consults. CutsceneState gathers them in one pass so the values agree with
	// each other.
	// ---------------------------------------------------------------------------
	struct CutsceneState
	{
		int dialogueKind;       // 0 none, 1 a box is up, 2 a box waits for a button
		int pacingMode;         // 1 means a voiced segment is pacing off the clock
		int fmvPlaybackState;   // 2 means an FMV owns the frame
		bool fmvScriptRunning;  // the script's own view of a movie
		bool fmvPlaying;        // the manager's "a video is playing" byte
		int atelStepCounter;    // the live ATEL context's own step number, -1 if unreadable
		bool anyCutsceneSignal; // any of the above says "this is not plain gameplay"
	};
	bool ReadCutsceneState(CutsceneState* out);

	// The cheap composite. True when ANY of the four signals is up. Use this for
	// "something scripted owns the screen", not for "the simulation is frozen",
	// which is ffx::IsSimulationPaused in MainLoop.h.
	bool CutsceneActive();

	// The nearest thing to a cutscene identity. There is no cutscene name or id in
	// the binary, so this is the live ATEL context's own per-step counter at
	// ctx + 500, which advances once per simulation step while the context is
	// active. Use it as a desync cross-check, not as a name. Returns false if the
	// context pointer is not usable yet.
	bool AtelStepCounter(int* out);

	// THE ONE OBSERVABLE A CO-OP LAYER SHOULD GATE INPUT ON. True while a dialogue
	// box, a voiced cutscene segment or an FMV owns the screen. Pure reads except
	// for the one call into FFX_Atel_MesWinBlockingKind, which only touches two
	// globals, so this is safe from the frame path.
	bool GameplayInputShouldBeIgnored();

	// ---------------------------------------------------------------------------
	// THE DIALOGUE BOX
	// ---------------------------------------------------------------------------

	// Values of DialogueState::kind, which is FFX_Atel_MesWinBlockingKind's return.
	const int dialogueNone = 0;
	const int dialogueOpen = 1;    // a box is up and is not waiting on the player
	const int dialogueWaiting = 2; // a box is up and WANTS A BUTTON PRESS

	struct DialogueState
	{
		int kind;           // dialogueNone, dialogueOpen or dialogueWaiting
		int windowIndex;    // the first window that is not idle, -1 if none
		WORD state;         // that window's MesWinRecord::State
		WORD attr;          // that window's message attribute
		BYTE flags;         // that window's MesWinRecord::Flags
		short chosenOption; // the choice it has recorded, -1 for none
		int openWindows;    // how many of the 8 are not idle
	};
	bool ReadDialogueState(DialogueState* out);

	// True while any message window is open at all. A pure read: the records are
	// walked here rather than through the game's own getter, so every dialogue
	// reader in this file is safe from any thread.
	bool DialogueOpen();

	// True only while a box is actually waiting for somebody to press confirm.
	// This is the state in which InjectDialogueConfirm does something.
	bool DialogueWaitingForInput();

	// The record for one of the 8 windows, with the double buffer resolved the same
	// way the game's own getter does it. Null if the index is out of range or the
	// array is not readable yet.
	BYTE* MessageWindow(int index);

	// The message text pointer a window is showing, read straight out of the
	// record. Returns NULL rather than an empty string if there is nothing there,
	// so a caller can tell "no box" from "an empty box".
	const char* MessageText(int windowIndex);

	// Ask the game to close a window, exactly as ATEL core syscall 107 does. This
	// is the script's own close path, so it is safe, but remember the SCRIPT is
	// what is waiting on the box: closing it from outside unblocks the script
	// thread and the cutscene carries on. Use it to make a remote advance land, not
	// to dismiss a box the local player has not answered.
	bool CloseDialogue(int windowIndex);

	// ---------------------------------------------------------------------------
	// LETTING EITHER PLAYER ADVANCE
	//
	// HookDialoguePad detours the message window's pad sampler. The callback runs
	// AFTER the game has filled the block from port 0 and BEFORE the 8 windows are
	// walked, which is the only window in the frame where an injected bit is both
	// unclobbered and seen.
	//
	// Returning false from the callback does not cancel anything, it is just there
	// so a gate can report that it declined to inject.
	//
	// The callback may be null, so the hook can be installed once at startup and
	// armed later.
	// ---------------------------------------------------------------------------
	typedef bool(__cdecl* DialoguePadFn)(void);

	bool HookDialoguePad(DialoguePadFn callback);
	bool DialoguePadHookInstalled();

	// OR the confirm bit into the shared pad block. Call this from inside a
	// DialoguePadFn callback and nowhere else, because anywhere else in the frame
	// either the sampler overwrites it or the windows have already been walked.
	// alsoHeld sets the held mask as well, which matters only for the held-repeat
	// paths in a choice list.
	bool InjectDialogueConfirm(bool alsoHeld);

	// The general form, for a remote mask that is more than just confirm. Same
	// timing rule. The mask is the 16-bit PS2 layout, so ffx::Btn::* values.
	bool InjectDialoguePad(WORD pressedMask, WORD heldMask);

	// REPLACES the block instead of OR-ing into it, which is the form a lockstep
	// layer needs.
	//
	// The difference matters and it is not a detail. InjectDialoguePad ORs, so a local
	// press and a remote press in the same step both land, which is exactly right for
	// "let either player advance a box". Under lockstep it is wrong: the sampler has
	// already filled the block from the LIVE local pad, which is undelayed and therefore
	// a different value on the two machines. OR-ing a replicated mask on top of that
	// leaves the local press in there, and the box advances a step early on whichever
	// machine the player is sitting at.
	//
	// So this one assigns. Everything the message windows see then comes from the
	// replicated mask and nothing comes from the hardware. Same timing rule as the other
	// two: from inside a DialoguePadFn callback and nowhere else.
	//
	// Note this also discards the engine's auto-repeat for the bits it covers, which is a
	// bonus rather than a loss. Those timers are wall-clock derived, first repeat at
	// 0.2333 s and then 0.1333 s, so a held direction in a choice list was never going to
	// be deterministic while the engine owned the edges. A caller that wants repeat has to
	// do its own, from the step counter rather than the clock.
	bool SetDialoguePad(WORD pressedMask, WORD heldMask);

	// What the block currently holds, for logging and for a desync check. Both
	// halves of the one dword the advance read tests.
	bool ReadDialoguePad(WORD* held, WORD* pressed);

	// ---------------------------------------------------------------------------
	// PACING AND THE TIMING TRACK
	//
	// PacingMode duplicates the read in MainLoop.h deliberately, so a plugin that
	// only cares about cutscenes does not have to include both. Same address, same
	// value.
	// ---------------------------------------------------------------------------
	int PacingMode();
	const char* PacingModeName(); // "accumulator" or "timing track, wall clock"

	struct TimingTrackState
	{
		int pacingMode;      // 1 while a track is driving the pacing
		bool trackSelected;  // the manager's cursor is armed
		bool mapHasSyncData; // this map shipped a sync file at all
		bool loadFinished;   // the async load completed, so a read will not block
		int loadedMapId;
		bool firstStep;   // the track has not taken its first step yet
		float nextTimeMs; // the entry just read
		float toleranceMs;
		float dueTimeMs;
		float elapsedSeconds;
		int extraStepsThisFrame; // how many catch-up steps the track asked for
		int stepScale88;         // the authored per-step motion scale in use
	};
	bool ReadTimingTrackState(TimingTrackState* out);

	// True while a voiced cutscene segment is pacing itself off the millisecond
	// clock. Equivalent to PacingMode() == 1, named for what it means.
	bool TimingTrackDriving();

	// TELL A RUNNING TIMING TRACK TO IGNORE SOME WALL-CLOCK TIME. This is the
	// shipped Esc-menu compensation, reached from outside. Call it with the number
	// of milliseconds a lockstep gate held the simulation, and the next
	// FFX_StepPacing folds it in and subtracts it from the track's elapsed time.
	//
	// It ADDS rather than overwrites, so a gate stall and an Esc-menu pause in the
	// same frame cannot lose one another. In pacing mode 0 nothing reads or clears
	// the value, so a write made outside a track just sits there, which is
	// harmless but means it is best written close to when it matters.
	bool AddTimingTrackPauseMs(DWORD milliseconds);
	bool TimingTrackPauseMs(DWORD* pending, DWORD* total);

	// Whether the engine is currently willing to skip frames at all. A long
	// per-scene exception list, and it consults both the dialogue observable and
	// the FMV one, which is the game's own evidence that those reads mean what this
	// file says. CALL FROM THE FRAME THREAD ONLY, it walks several subsystems.
	bool CatchUpAllowed();

	// ---------------------------------------------------------------------------
	// FMV
	//
	// An FMV cannot be lockstepped. It is WebM parsed by libwebm and decoded on its
	// own thread with a GetTickCount timebase, and the two players' machines will
	// never present the same video frame on the same simulation step.
	//
	// That costs almost nothing, because FFX_MainStep early-returns while an FMV
	// owns the frame, so NO SIMULATION RUNS. Both machines sit on the same
	// simulation step for the whole movie. Treat it as a barrier:
	//
	//   start   replicate FmvScriptRunning, which ATEL Movie syscalls set and
	//           clear, so it flips on the same opcode on both machines
	//   end     wait until BOTH machines report FmvPlaying() == 0 and
	//           FmvDecoderBusy() == false
	//   skip    make it a networked command, not a local input
	// ---------------------------------------------------------------------------
	struct FmvState
	{
		int playbackState;  // 2 means the movie owns the frame
		bool playing;       // the manager's byte
		bool hasPlayer;     // a player object exists and has not been released
		bool decoderBusy;   // a decode slot is still active or the queue is not drained
		bool scriptRunning; // the ATEL script's own view
		int videoId;        // -1 if the manager is not readable
		float width;
		float height;
		const char* path; // the resolved file path, NULL if unreadable
	};
	bool ReadFmvState(FmvState* out);

	// True while an FMV owns the frame and the simulation is therefore not running.
	// This is the barrier condition.
	bool FmvPlaying();

	// True on the frame the movie stopped owning the frame, computed by comparing
	// against the previous call. Call it once per frame from one place, or the edge
	// belongs to whoever called last. Returns false until it has been called twice.
	bool FmvJustEnded();

	// The script's own flag. This is the one to put on the wire, because it changes
	// on an opcode rather than on a decode-thread transition.
	bool FmvScriptRunning();

	// ---------------------------------------------------------------------------
	// THE CUTSCENE CAMERA
	//
	// One global handle per (class, screen), not per player. A cutscene opens a
	// class 3 Event handle, it outranks the class 1 player camera, and the client
	// sees the host's framing for free as long as the same script runs. So for the
	// stated requirement "the client should see what the host sees" there is
	// nothing to do.
	//
	// Handles are cached forever once opened, so a non-zero entry is NOT a
	// "cutscene is running" test. These readers exist to answer "has an event
	// camera ever been installed on this screen" and for logging.
	// ---------------------------------------------------------------------------
	const int cameraScreenCount = 3;

	struct CameraHandles
	{
		int event[cameraScreenCount];  // handle class 3
		int map[cameraScreenCount];    // handle class 1
		int battle[cameraScreenCount]; // handle class 5
		int viewer[cameraScreenCount]; // handle class 7
		bool matrixOverrideActive;     // the FMV and pre-rendered background path
	};
	bool ReadCameraHandles(CameraHandles* out);

	// True while something has forced the camera matrix, bypassing every mode and
	// every priority. The FMV path does this, so during a movie the camera is not
	// coming from a handle at all.
	bool CameraMatrixOverridden();

	// ---------------------------------------------------------------------------
	// Diagnostics
	//
	// Logs the whole picture once. Worth calling at startup to confirm the bindings
	// landed on something sensible, and during a cutscene on both machines when
	// something has gone out of step.
	// ---------------------------------------------------------------------------
	void LogCutsceneState();

} // namespace ffx
